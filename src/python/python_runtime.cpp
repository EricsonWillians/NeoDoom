//---------------------------------------------------------------------------
//
// BiasedDoom embedded Python runtime
//
// Python scripts are trusted native-equivalent mod code. The runtime is
// therefore opt-in and deliberately makes no claim of sandboxing CPython.
// ACS and ZScript continue to use their existing, independent runtimes.
//
//---------------------------------------------------------------------------

#include "python_runtime.h"
#include "python_game_api.h"

#include "actor.h"
#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "cmdlib.h"
#include "d_player.h"
#include "doomstat.h"
#include "filesystem.h"
#include "g_level.h"
#include "g_levellocals.h"
#include "i_interface.h"
#include "m_argv.h"
#include "p_local.h"
#include "p_spec.h"
#include "serializer.h"
#include "version.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef BIASEDDOOM_PYTHON
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#ifdef _PyCFunction_CAST
#define BD_PY_KEYWORD_FUNCTION(function) _PyCFunction_CAST(function)
#else
// CPython 3.10 predates _PyCFunction_CAST. This is the same two-step
// function-pointer cast used by CPython 3.11+ to avoid incompatible-signature
// warnings for METH_VARARGS | METH_KEYWORDS methods.
#define BD_PY_KEYWORD_FUNCTION(function) \
	reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)(void)>(function))
#endif
#endif

CVAR(Bool, py_enabled, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL | CVAR_SYSTEM_ONLY)
CVAR(Int, py_tick_budget_ms, 3, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
CVAR(Bool, py_tick_hard_budget, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
CVAR(Int, py_tick_overrun_limit, 3, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
CVAR(Int, py_max_tasks, 4096, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)

namespace PythonRuntime
{
#ifdef BIASEDDOOM_PYTHON

namespace
{
constexpr int PythonApiVersion = 2;

struct ScriptEntry
{
	int Container = -1;
	std::string Path;
	std::string Resource;
};

struct ScriptModule
{
	PyObject* Module = nullptr;
	int Container = -1;
	std::string Path;
};

struct Callback
{
	std::string Event;
	int EventIndex = -1;
	PyObject* Callable = nullptr;
	int Container = -1;
	std::string Source;
	PClassActor* ClassFilter = nullptr;
	int TidFilter = 0;
	int PlayerFilter = -1;
	unsigned Every = 1;
	int Priority = 0;
	bool Failed = false;
	bool BudgetDisabled = false;
	unsigned BudgetWarnings = 0;
	unsigned ConsecutiveOverruns = 0;
	uint64_t Seen = 0;
	uint64_t Calls = 0;
	uint64_t TotalMicroseconds = 0;
	uint64_t MaximumMicroseconds = 0;
	uint64_t BudgetSkips = 0;
	uint64_t BudgetOverruns = 0;
};

struct ScheduledTask
{
	uint64_t Id = 0;
	uint64_t DueTick = 0;
	uint64_t Interval = 0;
	PyObject* Callable = nullptr;
	int Container = -1;
	std::string Source;
	uint64_t MapSerial = 0;
	bool MapLocal = true;
	bool Cancelled = false;
	uint64_t Calls = 0;
	uint64_t TotalMicroseconds = 0;
	uint64_t MaximumMicroseconds = 0;
	uint64_t BudgetSkips = 0;
	uint64_t BudgetOverruns = 0;
	unsigned ConsecutiveOverruns = 0;
};

bool active = false;
bool inittabRegistered = false;
bool loadCallbackPending = false;
bool gameplayMutationBlocked = false;
bool callbacksNeedSort = false;
unsigned callbackDispatchDepth = 0;
uint64_t tickBudgetMicroseconds = 0;
uint64_t tickBudgetOverruns = 0;
uint64_t tickBudgetSkips = 0;
uint64_t taskClock = 0;
uint64_t nextTaskId = 1;
uint64_t mapSerial = 0;
unsigned taskDispatchDepth = 0;
int currentContainer = -1;
std::string currentSource;
std::string stdoutBuffer;
std::string stderrBuffer;
// dedup state for identical consecutive Python errors (see ReportPythonError)
FString s_lastPythonError;
unsigned s_repeatPythonErrorCount = 0;
// -scripttest: total reported errors, including suppressed repeats
unsigned s_pythonErrorCount = 0;
// -pyerrorlog <file>: JSON-lines feed of Python errors for external tools
std::string s_pythonErrorLogPath;
std::vector<ScriptEntry> discoveredScripts;
std::vector<ScriptModule> modules;
std::vector<Callback> callbacks;
std::vector<ScheduledTask> scheduledTasks;
std::thread::id engineThread;
PyObject* engineModule = nullptr;
PyObject* stateDictionary = nullptr; // Strong runtime-owned reference.

const char* const EventNames[] = {
	"engine_start",
	"map_load",
	"map_unload",
	"pre_tick",
	"tick",
	"post_tick",
	"actor_spawned",
	"actor_died",
	"actor_damaged",
	"actor_destroyed",
	"actor_revived",
	"line_activated",
	"line_activation_failed",
	"player_entered",
	"player_spawned",
	"player_respawned",
	"player_died",
	"player_disconnected",
	"save",
	"load",
	"engine_shutdown",
};
constexpr size_t EventCount = sizeof(EventNames) / sizeof(EventNames[0]);
std::array<bool, EventCount> eventHasCallbacks{};

int FindEventIndex(const char* event)
{
	for (size_t index = 0; index < EventCount; ++index)
	{
		if (strcmp(event, EventNames[index]) == 0) return static_cast<int>(index);
	}
	return -1;
}

void RebuildEventPresence()
{
	eventHasCallbacks.fill(false);
	for (const Callback& callback : callbacks)
	{
		if (!callback.Failed && !callback.BudgetDisabled && callback.EventIndex >= 0)
		{
			eventHasCallbacks[static_cast<size_t>(callback.EventIndex)] = true;
		}
	}
}

bool IsKnownEvent(const char* event)
{
	return FindEventIndex(event) >= 0;
}

bool HasCallbacks(const char* event)
{
	if (!active) return false;
	const int index = FindEventIndex(event);
	return index >= 0 && eventHasCallbacks[static_cast<size_t>(index)];
}

bool RuntimeRequested()
{
	if (Args != nullptr && Args->CheckParm("-nopython")) return false;
	return py_enabled || (Args != nullptr && Args->CheckParm("-python"));
}

bool CheckEngineThread()
{
	if (!active)
	{
		PyErr_SetString(PyExc_RuntimeError,
			"the biaseddoom API is unavailable while the interpreter is starting or shutting down");
		return false;
	}
	if (std::this_thread::get_id() == engineThread) return true;
	PyErr_SetString(PyExc_RuntimeError,
		"the biaseddoom API may only be called from the engine scripting thread");
	return false;
}

void EmitBufferedOutput(std::string& buffer, const char* text, bool flush, bool error)
{
	if (text != nullptr) buffer += text;

	size_t newline = 0;
	while ((newline = buffer.find('\n')) != std::string::npos)
	{
		std::string line = buffer.substr(0, newline);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (!line.empty())
		{
			Printf(error ? TEXTCOLOR_RED "[Python stderr] %s\n" : "[Python] %s\n", line.c_str());
		}
		buffer.erase(0, newline + 1);
	}

	if (flush && !buffer.empty())
	{
		Printf(error ? TEXTCOLOR_RED "[Python stderr] %s\n" : "[Python] %s\n", buffer.c_str());
		buffer.clear();
	}
}

std::string PyString(PyObject* object)
{
	if (object == nullptr) return {};
	PyObject* stringObject = PyObject_Str(object);
	if (stringObject == nullptr) return {};
	const char* text = PyUnicode_AsUTF8(stringObject);
	std::string result = text == nullptr ? std::string() : std::string(text);
	Py_DECREF(stringObject);
	return result;
}

std::string JsonEscape(const char* text)
{
	std::string out;
	if (text == nullptr) return out;
	for (const char* p = text; *p != 0; ++p)
	{
		switch (*p)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if ((unsigned char)*p >= 0x20) out += *p;
			break;
		}
	}
	return out;
}

// append one JSON line to the -pyerrorlog feed (no-op when not configured)
void WritePythonErrorLog(const char* context, const std::string& source,
	const FString& traceback, unsigned repeats, bool heartbeat)
{
	if (s_pythonErrorLogPath.empty()) return;

	FILE* file = fopen(s_pythonErrorLogPath.c_str(), "a");
	if (file == nullptr) return;

	const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	const char* map = primaryLevel != nullptr ? primaryLevel->MapName.GetChars() : "";

	fprintf(file,
		"{\"time_ms\":%lld,\"map\":\"%s\",\"context\":\"%s\",\"source\":\"%s\","
		"\"repeats_suppressed\":%u,\"heartbeat\":%s,\"traceback\":\"%s\"}\n",
		(long long)nowMs, JsonEscape(map).c_str(), JsonEscape(context).c_str(),
		JsonEscape(source.c_str()).c_str(), repeats, heartbeat ? "true" : "false",
		JsonEscape(traceback.GetChars()).c_str());
	fclose(file);
}

void ReportPythonError(const char* context, const std::string& source)
{
	if (!PyErr_Occurred()) return;

	s_pythonErrorCount++;

	PyObject* type = nullptr;
	PyObject* value = nullptr;
	PyObject* tracebackObject = nullptr;
	PyErr_Fetch(&type, &value, &tracebackObject);
	PyErr_NormalizeException(&type, &value, &tracebackObject);

	std::string formatted;
	PyObject* tracebackModule = PyImport_ImportModule("traceback");
	if (tracebackModule != nullptr)
	{
		PyObject* formatter = PyObject_GetAttrString(tracebackModule, "format_exception");
		if (formatter != nullptr && PyCallable_Check(formatter))
		{
			PyObject* lines = PyObject_CallFunctionObjArgs(formatter,
				type == nullptr ? Py_None : type,
				value == nullptr ? Py_None : value,
				tracebackObject == nullptr ? Py_None : tracebackObject,
				nullptr);
			if (lines != nullptr)
			{
				PyObject* separator = PyUnicode_FromString("");
				PyObject* joined = separator == nullptr ? nullptr : PyUnicode_Join(separator, lines);
				if (joined != nullptr)
				{
					const char* text = PyUnicode_AsUTF8(joined);
					if (text != nullptr) formatted = text;
					Py_DECREF(joined);
				}
				Py_XDECREF(separator);
				Py_DECREF(lines);
			}
		}
		Py_XDECREF(formatter);
		Py_DECREF(tracebackModule);
	}
	else
	{
		PyErr_Clear();
	}

	if (formatted.empty())
	{
		formatted = PyString(value != nullptr ? value : type);
	}

	// flush pending print() output first, so it appears before the error
	EmitBufferedOutput(stdoutBuffer, nullptr, true, false);
	EmitBufferedOutput(stderrBuffer, nullptr, true, true);

	FString full;
	full.Format("Python %s failed%s%s:\n%s%s",
		context,
		source.empty() ? "" : " in ",
		source.empty() ? "" : source.c_str(),
		formatted.c_str(),
		formatted.empty() || formatted.back() == '\n' ? "" : "\n");

	// suppress identical errors (a failing tick handler would otherwise
	// print a full traceback 35 times per second and flood the console)
	if (s_lastPythonError.IsNotEmpty() && full == s_lastPythonError)
	{
		s_repeatPythonErrorCount++;

		// periodic heartbeat, roughly every 10 seconds at full tic rate
		if (s_repeatPythonErrorCount % 350 == 0)
		{
			Printf(TEXTCOLOR_RED "(the previous Python error has now "
				"repeated %u times; duplicates suppressed)\n",
				s_repeatPythonErrorCount);
			WritePythonErrorLog(context, source, s_lastPythonError,
				s_repeatPythonErrorCount, true);
		}

		Py_XDECREF(type);
		Py_XDECREF(value);
		Py_XDECREF(tracebackObject);
		PyErr_Clear();
		return;
	}

	if (s_repeatPythonErrorCount > 0)
	{
		Printf(TEXTCOLOR_RED "(the previous Python error repeated %u more "
			"time%s before this one; duplicates suppressed)\n",
			s_repeatPythonErrorCount,
			s_repeatPythonErrorCount == 1 ? "" : "s");
		WritePythonErrorLog(context, source, s_lastPythonError,
			s_repeatPythonErrorCount, true);
		s_repeatPythonErrorCount = 0;
	}

	s_lastPythonError = full;

	Printf(TEXTCOLOR_RED "%s", full.GetChars());
	WritePythonErrorLog(context, source, full, 0, false);

	Py_XDECREF(type);
	Py_XDECREF(value);
	Py_XDECREF(tracebackObject);
	PyErr_Clear();
}

bool ValidateStateDictionary()
{
	if (engineModule == nullptr || stateDictionary == nullptr)
	{
		PyErr_SetString(PyExc_RuntimeError, "biaseddoom.state is unavailable");
		return false;
	}
	PyObject* attached = PyObject_GetAttrString(engineModule, "state");
	if (attached == nullptr)
	{
		PyErr_Clear();
		PyErr_SetString(PyExc_TypeError,
			"biaseddoom.state must not be deleted; mutate the existing dictionary in place");
		return false;
	}
	const bool valid = attached == stateDictionary;
	Py_DECREF(attached);
	if (!valid)
	{
		PyErr_SetString(PyExc_TypeError,
			"biaseddoom.state must not be rebound; mutate the existing dictionary in place");
	}
	return valid;
}

bool ValidResourcePath(const std::string& path, bool requirePythonExtension)
{
	if (path.empty() || path.front() == '/' || path.front() == '\\') return false;
	if (path.find('\0') != std::string::npos || path.find("..") != std::string::npos || path.find('\\') != std::string::npos) return false;
	return !requirePythonExtension || (path.size() >= 3 && path.substr(path.size() - 3) == ".py");
}

bool ReadResourceText(int container, const std::string& path, std::string& output, bool requirePythonExtension)
{
	if (!ValidResourcePath(path, requirePythonExtension)) return false;
	const int lump = fileSystem.CheckNumForFullName(path.c_str(), container);
	if (lump < 0) return false;
	auto data = fileSystem.ReadFile(lump);
	output.assign(data.string(), data.size());
	return true;
}

std::string Trim(std::string text)
{
	auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
	text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
	text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
	return text;
}

void DiscoverScripts()
{
	discoveredScripts.clear();
	int lastLump = 0;
	int manifest = -1;
	while ((manifest = fileSystem.FindLump("PYTHON", &lastLump, true)) >= 0)
	{
		const char* manifestPath = fileSystem.GetFileFullName(manifest);
		if (manifestPath == nullptr || stricmp(manifestPath, "PYTHON") != 0) continue;
		const int container = fileSystem.GetFileContainer(manifest);
		const char* resourceName = fileSystem.GetResourceFileFullName(container);
		const char* resourceLabel = resourceName == nullptr ? "<resource>" : resourceName;
		auto data = fileSystem.ReadFile(manifest);
		std::string contents(data.string(), data.size());
		size_t offset = 0;
		unsigned lineNumber = 0;
		while (offset <= contents.size())
		{
			const size_t end = contents.find('\n', offset);
			std::string line = contents.substr(offset, end == std::string::npos ? std::string::npos : end - offset);
			offset = end == std::string::npos ? contents.size() + 1 : end + 1;
			++lineNumber;
			if (lineNumber == 1 && line.size() >= 3 &&
				static_cast<unsigned char>(line[0]) == 0xef &&
				static_cast<unsigned char>(line[1]) == 0xbb &&
				static_cast<unsigned char>(line[2]) == 0xbf)
			{
				line.erase(0, 3);
			}

			const size_t comment = line.find('#');
			if (comment != std::string::npos) line.erase(comment);
			line = Trim(std::move(line));
			if (line.size() >= 2 && ((line.front() == '"' && line.back() == '"') ||
				(line.front() == '\'' && line.back() == '\'')))
			{
				line = line.substr(1, line.size() - 2);
			}
			if (line.empty()) continue;

			if (!ValidResourcePath(line, true))
			{
				Printf(TEXTCOLOR_RED "Invalid Python script path at %s:PYTHON:%u: %s\n",
					resourceLabel, lineNumber, line.c_str());
				continue;
			}
			if (fileSystem.CheckNumForFullName(line.c_str(), container) < 0)
			{
				Printf(TEXTCOLOR_RED "Python script %s was not found in %s (manifest line %u)\n",
					line.c_str(), resourceLabel, lineNumber);
				continue;
			}

			discoveredScripts.push_back({ container, line, resourceLabel });
		}
	}
}

void DictSet(PyObject* dictionary, const char* key, PyObject* value)
{
	if (dictionary == nullptr || value == nullptr)
	{
		Py_XDECREF(value);
		return;
	}
	PyDict_SetItemString(dictionary, key, value);
	Py_DECREF(value);
}

void DictSetString(PyObject* dictionary, const char* key, const char* value)
{
	DictSet(dictionary, key, PyUnicode_FromString(value == nullptr ? "" : value));
}

void DictSetInt(PyObject* dictionary, const char* key, long long value)
{
	DictSet(dictionary, key, PyLong_FromLongLong(value));
}

void DictSetFloat(PyObject* dictionary, const char* key, double value)
{
	DictSet(dictionary, key, PyFloat_FromDouble(value));
}

void DictSetBool(PyObject* dictionary, const char* key, bool value)
{
	DictSet(dictionary, key, PyBool_FromLong(value ? 1 : 0));
}

int ActorPlayerNumber(AActor* actor)
{
	if (actor == nullptr || actor->player == nullptr) return -1;
	for (unsigned i = 0; i < MAXPLAYERS; ++i)
	{
		if (&players[i] == actor->player) return static_cast<int>(i);
	}
	return -1;
}

PyObject* ActorSnapshot(AActor* actor)
{
	if (actor == nullptr || (actor->ObjectFlags & OF_EuthanizeMe))
	{
		Py_RETURN_NONE;
	}

	PyObject* result = PyDict_New();
	DictSetString(result, "class_name", actor->GetClass()->TypeName.GetChars());
	DictSetInt(result, "tid", actor->tid);
	DictSetInt(result, "health", actor->health);
	DictSetFloat(result, "x", actor->X());
	DictSetFloat(result, "y", actor->Y());
	DictSetFloat(result, "z", actor->Z());
	DictSetFloat(result, "angle", actor->Angles.Yaw.Degrees());
	DictSetFloat(result, "pitch", actor->Angles.Pitch.Degrees());
	DictSetFloat(result, "velocity_x", actor->Vel.X);
	DictSetFloat(result, "velocity_y", actor->Vel.Y);
	DictSetFloat(result, "velocity_z", actor->Vel.Z);
	DictSetBool(result, "alive", actor->health > 0);
	DictSetBool(result, "is_monster", (actor->flags3 & MF3_ISMONSTER) != 0);
	DictSetBool(result, "is_player", actor->player != nullptr);
	DictSetInt(result, "player_index", ActorPlayerNumber(actor));
	DictSet(result, "ref", GameApi::MakeActorRef(actor));
	return result;
}

AActor* FindActor(int tid)
{
	if (primaryLevel == nullptr || tid == 0) return nullptr;
	return primaryLevel->GetActorIterator(tid).Next();
}

bool CheckSessionMutationAllowed()
{
	if (netgame || multiplayer || demoplayback || demorecording)
	{
		PyErr_SetString(PyExc_RuntimeError,
			"Python gameplay mutations are disabled in multiplayer and demo sessions to protect synchronization");
		return false;
	}
	return true;
}

bool CheckMutationAllowed()
{
	if (!CheckSessionMutationAllowed()) return false;
	if (gameplayMutationBlocked)
	{
		PyErr_SetString(PyExc_RuntimeError,
			"Python gameplay mutations are unavailable during save and world-unload callbacks");
		return false;
	}
	if (primaryLevel == nullptr || primaryLevel->MapName.IsEmpty())
	{
		PyErr_SetString(PyExc_RuntimeError, "no level is currently active");
		return false;
	}
	return true;
}

bool RegisterCallback(const char* event, PyObject* callable, int container, const std::string& source,
	unsigned every = 1, int priority = 0, const char* className = nullptr,
	int tidFilter = 0, int playerFilter = -1)
{
	const int eventIndex = FindEventIndex(event);
	if (eventIndex < 0)
	{
		PyErr_Format(PyExc_ValueError, "unknown BiasedDoom event '%s'", event);
		return false;
	}
	if (!PyCallable_Check(callable))
	{
		PyErr_SetString(PyExc_TypeError, "callback must be callable");
		return false;
	}
	if (every == 0 || every > 1000000)
	{
		PyErr_SetString(PyExc_ValueError, "callback every must be between 1 and 1000000");
		return false;
	}
	if (playerFilter < -1 || playerFilter >= static_cast<int>(MAXPLAYERS))
	{
		PyErr_Format(PyExc_ValueError, "callback player filter must be -1..%zu", MAXPLAYERS - 1);
		return false;
	}
	PClassActor* classFilter = nullptr;
	if (className != nullptr)
	{
		classFilter = PClass::FindActor(FName(className));
		if (classFilter == nullptr)
		{
			PyErr_Format(PyExc_ValueError, "unknown actor class filter '%s'", className);
			return false;
		}
	}

	for (const Callback& existing : callbacks)
	{
		if (existing.Event == event && existing.Callable == callable) return true;
	}

	Py_INCREF(callable);
	Callback callback;
	callback.Event = event;
	callback.EventIndex = eventIndex;
	callback.Callable = callable;
	callback.Container = container;
	callback.Source = source;
	callback.ClassFilter = classFilter;
	callback.TidFilter = tidFilter;
	callback.PlayerFilter = playerFilter;
	callback.Every = every;
	callback.Priority = priority;
	callbacks.push_back(std::move(callback));
	eventHasCallbacks[static_cast<size_t>(eventIndex)] = true;
	callbacksNeedSort = true;
	return true;
}

PyObject* BuildEvent(const char* name)
{
	PyObject* event = PyDict_New();
	DictSetString(event, "name", name);
	DictSetString(event, "map", primaryLevel == nullptr ? "" : primaryLevel->MapName.GetChars());
	DictSetInt(event, "level_time", primaryLevel == nullptr ? 0 : primaryLevel->time);
	return event;
}

void SortCallbacks()
{
	if (!callbacksNeedSort || callbackDispatchDepth != 0) return;
	std::stable_sort(callbacks.begin(), callbacks.end(), [](const Callback& left, const Callback& right)
	{
		return left.Priority > right.Priority;
	});
	callbacksNeedSort = false;
}

bool CallbackMatches(Callback& callback, AActor* subject, int playerIndex)
{
	if (callback.ClassFilter != nullptr && (subject == nullptr || !subject->IsKindOf(callback.ClassFilter))) return false;
	if (callback.TidFilter != 0 && (subject == nullptr || subject->tid != callback.TidFilter)) return false;
	if (callback.PlayerFilter >= 0)
	{
		const int actualPlayer = playerIndex >= 0 ? playerIndex : ActorPlayerNumber(subject);
		if (actualPlayer != callback.PlayerFilter) return false;
	}
	++callback.Seen;
	return callback.Every == 1 || ((callback.Seen - 1) % callback.Every) == 0;
}

bool IsTickEvent(const char* eventName)
{
	return strcmp(eventName, "pre_tick") == 0 || strcmp(eventName, "tick") == 0 ||
		strcmp(eventName, "post_tick") == 0;
}

void InvokeEvent(const char* eventName, PyObject* event, AActor* subject = nullptr, int playerIndex = -1)
{
	if (!active)
	{
		Py_XDECREF(event);
		return;
	}
	if (event == nullptr) event = BuildEvent(eventName);

	SortCallbacks();
	PyObject* arguments = PyTuple_Pack(1, event);
	if (arguments == nullptr)
	{
		Py_DECREF(event);
		ReportPythonError(eventName, "event argument construction");
		return;
	}
	const int previousContainer = currentContainer;
	const std::string previousSource = currentSource;
	// A running callback may import another script whose decorators append to
	// this vector. Iterate only the callbacks present at dispatch start and do
	// not retain references across PyObject_CallObject(), which can reallocate
	// the vector. Newly registered callbacks begin with the next event.
	const size_t callbackCount = callbacks.size();
	++callbackDispatchDepth;
	const bool tickEvent = IsTickEvent(eventName);
	const int eventIndex = FindEventIndex(eventName);
	bool callbackAvailabilityChanged = false;
	for (size_t index = 0; index < callbackCount; ++index)
	{
		if (callbacks[index].Failed || callbacks[index].BudgetDisabled || callbacks[index].EventIndex != eventIndex) continue;
		if (!CallbackMatches(callbacks[index], subject, playerIndex)) continue;
		const uint64_t hardLimit = py_tick_budget_ms > 0
			? static_cast<uint64_t>(static_cast<int>(py_tick_budget_ms)) * 1000u : 0;
		if (tickEvent && py_tick_hard_budget && hardLimit > 0 && tickBudgetMicroseconds >= hardLimit)
		{
			++callbacks[index].BudgetSkips;
			++tickBudgetSkips;
			continue;
		}

		const int container = callbacks[index].Container;
		const std::string source = callbacks[index].Source;
		PyObject* const callable = callbacks[index].Callable;
		currentContainer = container;
		currentSource = source;
		const auto start = std::chrono::steady_clock::now();
		PyObject* result = PyObject_CallObject(callable, arguments);
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - start).count();
		++callbacks[index].Calls;
		callbacks[index].TotalMicroseconds += static_cast<uint64_t>(std::max<int64_t>(0, elapsed));
		callbacks[index].MaximumMicroseconds = std::max(callbacks[index].MaximumMicroseconds,
			static_cast<uint64_t>(std::max<int64_t>(0, elapsed)));
		const uint64_t elapsedMicroseconds = static_cast<uint64_t>(std::max<int64_t>(0, elapsed));
		if (tickEvent) tickBudgetMicroseconds += elapsedMicroseconds;
		if (result == nullptr)
		{
			ReportPythonError(eventName, source);
			callbacks[index].Failed = true;
			callbackAvailabilityChanged = true;
			Printf(TEXTCOLOR_YELLOW "Python callback %s in %s has been disabled until py_reload.\n",
				eventName, source.c_str());
		}
		else
		{
			Py_DECREF(result);
		}

		if (tickEvent && py_tick_budget_ms > 0 && elapsed > static_cast<int64_t>(static_cast<int>(py_tick_budget_ms)) * 1000 && callbacks[index].BudgetWarnings < 3)
		{
			++callbacks[index].BudgetWarnings;
			Printf(TEXTCOLOR_YELLOW "Python %s callback in %s took %.3f ms (whole-tic budget: %d ms).\n",
				 eventName, source.c_str(), elapsed / 1000.0, static_cast<int>(py_tick_budget_ms));
		}
		if (tickEvent && py_tick_hard_budget && hardLimit > 0 && elapsedMicroseconds >= hardLimit)
		{
			++callbacks[index].BudgetOverruns;
			++callbacks[index].ConsecutiveOverruns;
			++tickBudgetOverruns;
			const int overrunLimit = static_cast<int>(py_tick_overrun_limit);
			if (overrunLimit > 0 && callbacks[index].ConsecutiveOverruns >= static_cast<unsigned>(overrunLimit))
			{
				callbacks[index].BudgetDisabled = true;
				callbackAvailabilityChanged = true;
				Printf(TEXTCOLOR_YELLOW "Python %s callback in %s exceeded the %d ms budget %u consecutive times and was disabled until py_reload.\n",
					eventName, source.c_str(), static_cast<int>(py_tick_budget_ms),
					callbacks[index].ConsecutiveOverruns);
			}
		}
		else if (tickEvent)
		{
			callbacks[index].ConsecutiveOverruns = 0;
		}
	}
	--callbackDispatchDepth;
	if (callbackAvailabilityChanged) RebuildEventPresence();
	SortCallbacks();
	// Actor APIs can synchronously dispatch nested spawn/death events. Restore
	// the caller's resource context so it can keep using same-mod VFS helpers.
	currentContainer = previousContainer;
	currentSource = previousSource;
	Py_DECREF(arguments);
	Py_DECREF(event);
}

void CleanupScheduledTasks()
{
	if (taskDispatchDepth != 0) return;
	for (ScheduledTask& task : scheduledTasks)
	{
		if (task.Cancelled) Py_CLEAR(task.Callable);
	}
	scheduledTasks.erase(std::remove_if(scheduledTasks.begin(), scheduledTasks.end(),
		[](const ScheduledTask& task) { return task.Cancelled; }), scheduledTasks.end());
}

void CancelMapLocalTasks()
{
	for (ScheduledTask& task : scheduledTasks)
	{
		if (task.MapLocal) task.Cancelled = true;
	}
	CleanupScheduledTasks();
}

void ProcessScheduledTasks()
{
	if (!active || scheduledTasks.empty()) return;
	const size_t taskCount = scheduledTasks.size();
	++taskDispatchDepth;
	for (size_t index = 0; index < taskCount; ++index)
	{
		if (scheduledTasks[index].Cancelled || scheduledTasks[index].DueTick > taskClock) continue;
		if (scheduledTasks[index].MapLocal && scheduledTasks[index].MapSerial != mapSerial)
		{
			scheduledTasks[index].Cancelled = true;
			continue;
		}
		const uint64_t hardLimit = py_tick_budget_ms > 0
			? static_cast<uint64_t>(static_cast<int>(py_tick_budget_ms)) * 1000u : 0;
		if (py_tick_hard_budget && hardLimit > 0 && tickBudgetMicroseconds >= hardLimit)
		{
			++scheduledTasks[index].BudgetSkips;
			++tickBudgetSkips;
			continue;
		}

		const int previousContainer = currentContainer;
		const std::string previousSource = currentSource;
		currentContainer = scheduledTasks[index].Container;
		currentSource = scheduledTasks[index].Source;
		PyObject* callable = scheduledTasks[index].Callable;
		const uint64_t id = scheduledTasks[index].Id;
		const auto start = std::chrono::steady_clock::now();
		PyObject* result = PyObject_CallNoArgs(callable);
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - start).count();
		currentContainer = previousContainer;
		currentSource = previousSource;

		// New tasks can reallocate the vector, so reacquire by stable ID.
		auto taskIterator = std::find_if(scheduledTasks.begin(), scheduledTasks.end(),
			[id](const ScheduledTask& task) { return task.Id == id; });
		if (taskIterator == scheduledTasks.end())
		{
			Py_XDECREF(result);
			continue;
		}
		ScheduledTask& task = *taskIterator;
		++task.Calls;
		const uint64_t elapsedUs = static_cast<uint64_t>(std::max<int64_t>(0, elapsed));
		task.TotalMicroseconds += elapsedUs;
		task.MaximumMicroseconds = std::max(task.MaximumMicroseconds, elapsedUs);
		tickBudgetMicroseconds += elapsedUs;
		if (py_tick_hard_budget && hardLimit > 0 && elapsedUs >= hardLimit)
		{
			++task.BudgetOverruns;
			++task.ConsecutiveOverruns;
			++tickBudgetOverruns;
			const int overrunLimit = static_cast<int>(py_tick_overrun_limit);
			if (overrunLimit > 0 && task.ConsecutiveOverruns >= static_cast<unsigned>(overrunLimit))
			{
				task.Cancelled = true;
				Printf(TEXTCOLOR_YELLOW "Python scheduled task in %s exceeded the %d ms budget %u consecutive times and was cancelled.\n",
					task.Source.c_str(), static_cast<int>(py_tick_budget_ms), task.ConsecutiveOverruns);
			}
		}
		else
		{
			task.ConsecutiveOverruns = 0;
		}
		if (result == nullptr)
		{
			ReportPythonError("scheduled task", task.Source);
			task.Cancelled = true;
		}
		else
		{
			if (result == Py_False) task.Cancelled = true;
			Py_DECREF(result);
		}
		if (!task.Cancelled)
		{
			if (task.Interval == 0) task.Cancelled = true;
			else task.DueTick = taskClock + task.Interval;
		}
	}
	--taskDispatchDepth;
	CleanupScheduledTasks();
}

PyObject* PyBdLog(PyObject*, PyObject* args, PyObject* kwargs)
{
	if (!CheckEngineThread()) return nullptr;
	PyObject* message = nullptr;
	const char* level = "info";
	static const char* keywords[] = { "message", "level", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|s:log", const_cast<char**>(keywords), &message, &level)) return nullptr;
	const std::string text = PyString(message);
	if (PyErr_Occurred()) return nullptr;
	const bool isError = stricmp(level, "error") == 0;
	const bool isWarning = stricmp(level, "warning") == 0 || stricmp(level, "warn") == 0;
	if (isError) Printf(TEXTCOLOR_RED "[Python:error] %s\n", text.c_str());
	else if (isWarning) Printf(TEXTCOLOR_YELLOW "[Python:warning] %s\n", text.c_str());
	else Printf("[Python:%s] %s\n", level, text.c_str());
	Py_RETURN_NONE;
}

PyObject* PyBdWriteOutput(PyObject*, PyObject* args)
{
	if (!CheckEngineThread()) return nullptr;
	const char* text = nullptr;
	int stream = 0;
	int flush = 0;
	if (!PyArg_ParseTuple(args, "sii:_write_output", &text, &stream, &flush)) return nullptr;
	EmitBufferedOutput(stream == 0 ? stdoutBuffer : stderrBuffer, text, flush != 0, stream != 0);
	Py_RETURN_NONE;
}

PyObject* PyBdRegisterCallback(PyObject*, PyObject* args, PyObject* kwargs)
{
	if (!CheckEngineThread()) return nullptr;
	const char* event = nullptr;
	PyObject* callable = nullptr;
	unsigned every = 1;
	int priority = 0;
	const char* className = nullptr;
	int tid = 0;
	int player = -1;
	static const char* keywords[] = { "event", "callback", "every", "priority", "class_name", "tid", "player", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sO|Iizii:_register_callback", const_cast<char**>(keywords),
		&event, &callable, &every, &priority, &className, &tid, &player)) return nullptr;
	if (!RegisterCallback(event, callable, currentContainer, currentSource, every, priority, className, tid, player)) return nullptr;
	Py_RETURN_NONE;
}

PyObject* PyBdCurrentMap(PyObject*, PyObject*)
{
	if (!CheckEngineThread()) return nullptr;
	if (primaryLevel == nullptr || primaryLevel->MapName.IsEmpty()) Py_RETURN_NONE;
	return PyUnicode_FromString(primaryLevel->MapName.GetChars());
}

PyObject* PyBdLevelTime(PyObject*, PyObject*)
{
	if (!CheckEngineThread()) return nullptr;
	return PyLong_FromLong(primaryLevel == nullptr ? 0 : primaryLevel->time);
}

PyObject* PyBdPlayers(PyObject*, PyObject*)
{
	if (!CheckEngineThread()) return nullptr;
	PyObject* result = PyList_New(0);
	for (unsigned i = 0; i < MAXPLAYERS; ++i)
	{
		if (!playeringame[i]) continue;
		PyObject* player = PyDict_New();
		DictSetInt(player, "index", i);
		DictSetString(player, "name", players[i].userinfo.GetName());
		DictSetBool(player, "in_game", true);
		DictSet(player, "actor", ActorSnapshot(players[i].mo));
		PyList_Append(result, player);
		Py_DECREF(player);
	}
	return result;
}

PyObject* PyBdActors(PyObject*, PyObject* args, PyObject* kwargs)
{
	if (!CheckEngineThread()) return nullptr;
	const char* className = nullptr;
	int tid = 0;
	int limit = 1024;
	static const char* keywords[] = { "class_name", "tid", "limit", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|zii:actors", const_cast<char**>(keywords), &className, &tid, &limit)) return nullptr;
	if (limit < 1 || limit > 100000)
	{
		PyErr_SetString(PyExc_ValueError, "limit must be between 1 and 100000");
		return nullptr;
	}

	PClassActor* classFilter = nullptr;
	if (className != nullptr)
	{
		classFilter = PClass::FindActor(FName(className));
		if (classFilter == nullptr)
		{
			PyErr_Format(PyExc_ValueError, "unknown actor class '%s'", className);
			return nullptr;
		}
	}

	PyObject* result = PyList_New(0);
	if (primaryLevel == nullptr) return result;
	if (tid != 0)
	{
		auto iterator = primaryLevel->GetActorIterator(tid);
		AActor* actor = nullptr;
		while (PyList_Size(result) < limit && (actor = iterator.Next()) != nullptr)
		{
			if (classFilter != nullptr && !actor->IsKindOf(classFilter)) continue;
			PyObject* snapshot = ActorSnapshot(actor);
			PyList_Append(result, snapshot);
			Py_DECREF(snapshot);
		}
	}
	else
	{
		auto iterator = primaryLevel->GetThinkerIterator<AActor>();
		AActor* actor = nullptr;
		while (PyList_Size(result) < limit && (actor = iterator.Next()) != nullptr)
		{
			if (classFilter != nullptr && !actor->IsKindOf(classFilter)) continue;
			PyObject* snapshot = ActorSnapshot(actor);
			PyList_Append(result, snapshot);
			Py_DECREF(snapshot);
		}
	}
	return result;
}

PyObject* PyBdActor(PyObject*, PyObject* args)
{
	if (!CheckEngineThread()) return nullptr;
	int tid = 0;
	if (!PyArg_ParseTuple(args, "i:actor", &tid)) return nullptr;
	return ActorSnapshot(FindActor(tid));
}

PyObject* PyBdSpawnActor(PyObject*, PyObject* args, PyObject* kwargs)
{
	if (!CheckEngineThread()) return nullptr;
	const char* className = nullptr;
	double x = 0, y = 0, z = 0, angle = 0;
	int tid = 0;
	int force = 0;
	static const char* keywords[] = { "class_name", "x", "y", "z", "angle", "tid", "force", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sddd|dii:spawn_actor", const_cast<char**>(keywords),
		&className, &x, &y, &z, &angle, &tid, &force)) return nullptr;
	if (!CheckMutationAllowed()) return nullptr;

	PClassActor* actorClass = PClass::FindActor(FName(className));
	if (actorClass == nullptr)
	{
		PyErr_Format(PyExc_ValueError, "unknown actor class '%s'", className);
		return nullptr;
	}
	AActor* actor = Spawn(primaryLevel, actorClass, DVector3(x, y, z), ALLOW_REPLACE);
	if (actor == nullptr)
	{
		PyErr_SetString(PyExc_RuntimeError, "actor creation failed");
		return nullptr;
	}
	if (!force && !P_TestMobjLocation(actor))
	{
		actor->ClearCounters();
		actor->Destroy();
		PyErr_SetString(PyExc_RuntimeError, "actor does not fit at the requested position; pass force=True to override");
		return nullptr;
	}
	actor->Angles.Yaw = DAngle::fromDeg(angle);
	if (tid == 0) tid = primaryLevel->FindUniqueTID(10000, 100000, false);
	if (tid == 0)
	{
		actor->Destroy();
		PyErr_SetString(PyExc_RuntimeError, "could not allocate a unique TID");
		return nullptr;
	}
	actor->SetTID(tid);
	return ActorSnapshot(actor);
}

// actor class registry for the bootstrap's bd.actors helper: returns a
// list of (class_name, parent_name, kind_mask) tuples for every
// non-abstract Actor descendant known to the engine (including mod- and
// script-defined classes). kind_mask bits: 1=monster, 2=projectile,
// 4=weapon, 8=inventory item, 16=player pawn.
PyObject* PyBdActorClassInfo(PyObject*, PyObject*)
{
	if (!CheckEngineThread()) return nullptr;

	static PClassActor* actorBase = PClass::FindActor("Actor");
	static PClassActor* weaponBase = PClass::FindActor("Weapon");
	static PClassActor* inventoryBase = PClass::FindActor("Inventory");
	static PClassActor* playerBase = PClass::FindActor("PlayerPawn");

	PyObject* result = PyList_New(0);
	if (result == nullptr) return nullptr;

	for (PClass* cls : PClass::AllClasses)
	{
		if (cls == nullptr || cls->bAbstract) continue;
		if (actorBase != nullptr && !cls->IsDescendantOf(actorBase)) continue;

		int kind = 0;
		const AActor* def = (const AActor*)cls->Defaults;
		if (def != nullptr)
		{
			if ((def->flags & MF_SHOOTABLE) && (def->flags & MF_COUNTKILL)) kind |= 1;
			if (def->flags & MF_MISSILE) kind |= 2;
		}
		if (weaponBase != nullptr && cls->IsDescendantOf(weaponBase)) kind |= 4;
		if (inventoryBase != nullptr && cls->IsDescendantOf(inventoryBase)) kind |= 8;
		if (playerBase != nullptr && cls->IsDescendantOf(playerBase)) kind |= 16;

		const char* parent = cls->ParentClass != nullptr ? cls->ParentClass->TypeName.GetChars() : "";
		PyObject* entry = Py_BuildValue("(ssi)", cls->TypeName.GetChars(), parent, kind);
		if (entry == nullptr || PyList_Append(result, entry) < 0)
		{
			Py_XDECREF(entry);
			Py_DECREF(result);
			return nullptr;
		}
		Py_DECREF(entry);
	}

	return result;
}

PyObject* PyBdDamageActor(PyObject*, PyObject* args, PyObject* kwargs)
{
	if (!CheckEngineThread()) return nullptr;
	int tid = 0;
	int damage = 0;
	const char* damageType = "None";
	static const char* keywords[] = { "tid", "damage", "damage_type", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ii|s:damage_actor", const_cast<char**>(keywords),
		&tid, &damage, &damageType)) return nullptr;
	if (!CheckMutationAllowed()) return nullptr;
	AActor* actor = FindActor(tid);
	if (actor == nullptr)
	{
		PyErr_Format(PyExc_LookupError, "no actor with TID %d", tid);
		return nullptr;
	}
	return PyLong_FromLong(P_DamageMobj(actor, nullptr, nullptr, damage, FName(damageType)));
}

PyObject* PyBdSetActorVelocity(PyObject*, PyObject* args)
{
	if (!CheckEngineThread()) return nullptr;
	int tid = 0;
	double x = 0, y = 0, z = 0;
	if (!PyArg_ParseTuple(args, "iddd:set_actor_velocity", &tid, &x, &y, &z)) return nullptr;
	if (!CheckMutationAllowed()) return nullptr;
	AActor* actor = FindActor(tid);
	if (actor == nullptr)
	{
		PyErr_Format(PyExc_LookupError, "no actor with TID %d", tid);
		return nullptr;
	}
	actor->Vel = DVector3(x, y, z);
	return ActorSnapshot(actor);
}

PyObject* PyBdDestroyActor(PyObject*, PyObject* args)
{
	if (!CheckEngineThread()) return nullptr;
	int tid = 0;
	if (!PyArg_ParseTuple(args, "i:destroy_actor", &tid)) return nullptr;
	if (!CheckMutationAllowed()) return nullptr;
	AActor* actor = FindActor(tid);
	if (actor == nullptr) Py_RETURN_FALSE;
	actor->ClearCounters();
	actor->Destroy();
	Py_RETURN_TRUE;
}

PyObject* CVarToPython(FBaseCVar* cvar)
{
	switch (cvar->GetRealType())
	{
	case CVAR_Bool: return PyBool_FromLong(cvar->GetGenericRep(CVAR_Bool).Bool ? 1 : 0);
	case CVAR_Int:
	case CVAR_Color: return PyLong_FromLong(cvar->GetGenericRep(CVAR_Int).Int);
	case CVAR_Float: return PyFloat_FromDouble(cvar->GetGenericRep(CVAR_Float).Float);
	default: return PyUnicode_FromString(cvar->GetGenericRep(CVAR_String).String);
	}
}

PyObject* PyBdGetCVar(PyObject*, PyObject* args)
{
	if (!CheckEngineThread()) return nullptr;
	const char* name = nullptr;
	if (!PyArg_ParseTuple(args, "s:get_cvar", &name)) return nullptr;
	FBaseCVar* cvar = FindCVar(name, nullptr);
	if (cvar == nullptr)
	{
		PyErr_Format(PyExc_KeyError, "unknown CVar '%s'", name);
		return nullptr;
	}
	return CVarToPython(cvar);
}

PyObject* PyBdSetCVar(PyObject*, PyObject* args)
{
	if (!CheckEngineThread()) return nullptr;
	const char* name = nullptr;
	PyObject* value = nullptr;
	if (!PyArg_ParseTuple(args, "sO:set_cvar", &name, &value)) return nullptr;
	if (!CheckSessionMutationAllowed()) return nullptr;
	FBaseCVar* cvar = FindCVar(name, nullptr);
	if (cvar == nullptr)
	{
		PyErr_Format(PyExc_KeyError, "unknown CVar '%s'", name);
		return nullptr;
	}
	const int flags = cvar->GetFlags();
	if (flags & (CVAR_NOSET | CVAR_SYSTEM_ONLY | CVAR_IGNORE))
	{
		PyErr_Format(PyExc_PermissionError, "CVar '%s' is not writable by mod scripts", name);
		return nullptr;
	}
	if ((flags & CVAR_CHEAT) && sysCallbacks.CheckCheatmode != nullptr &&
		sysCallbacks.CheckCheatmode(false, false))
	{
		PyErr_Format(PyExc_PermissionError, "CVar '%s' is currently cheat-protected", name);
		return nullptr;
	}

	switch (cvar->GetRealType())
	{
	case CVAR_Bool:
	{
		const int truth = PyObject_IsTrue(value);
		if (truth < 0) return nullptr;
		cvar->SetGenericRep(UCVarValue(truth != 0), CVAR_Bool);
		break;
	}
	case CVAR_Int:
	case CVAR_Color:
	{
		const long number = PyLong_AsLong(value);
		if (PyErr_Occurred()) return nullptr;
		if (number < INT_MIN || number > INT_MAX)
		{
			PyErr_SetString(PyExc_OverflowError, "CVar integer is outside the engine's 32-bit range");
			return nullptr;
		}
		cvar->SetGenericRep(UCVarValue(static_cast<int>(number)), CVAR_Int);
		break;
	}
	case CVAR_Float:
	{
		const double number = PyFloat_AsDouble(value);
		if (PyErr_Occurred()) return nullptr;
		cvar->SetGenericRep(UCVarValue(number), CVAR_Float);
		break;
	}
	default:
	{
		const std::string text = PyString(value);
		if (PyErr_Occurred()) return nullptr;
		cvar->SetGenericRep(UCVarValue(text.c_str()), CVAR_String);
		break;
	}
	}
	return CVarToPython(cvar);
}

PyObject* PyBdExecute(PyObject*, PyObject* args)
{
	if (!CheckEngineThread()) return nullptr;
	const char* command = nullptr;
	if (!PyArg_ParseTuple(args, "s:execute", &command)) return nullptr;
	if (!CheckSessionMutationAllowed()) return nullptr;
	AddCommandString(command);
	Py_RETURN_NONE;
}

PyObject* PyBdExecuteACS(PyObject*, PyObject* args, PyObject* kwargs)
{
	if (!CheckEngineThread()) return nullptr;
	PyObject* scriptObject = nullptr;
	PyObject* argumentsObject = Py_None;
	int always = 0;
	int wantResult = 0;
	static const char* keywords[] = { "script", "arguments", "always", "want_result", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|Opp:execute_acs", const_cast<char**>(keywords),
		&scriptObject, &argumentsObject, &always, &wantResult)) return nullptr;
	if (!CheckMutationAllowed()) return nullptr;

	int script = 0;
	if (PyLong_Check(scriptObject))
	{
		const long number = PyLong_AsLong(scriptObject);
		if (PyErr_Occurred()) return nullptr;
		if (number < INT_MIN || number > INT_MAX)
		{
			PyErr_SetString(PyExc_OverflowError, "ACS script number is outside the engine's 32-bit range");
			return nullptr;
		}
		script = static_cast<int>(number);
	}
	else if (PyUnicode_Check(scriptObject))
	{
		const char* name = PyUnicode_AsUTF8(scriptObject);
		if (name == nullptr) return nullptr;
		script = -FName(name).GetIndex();
	}
	else
	{
		PyErr_SetString(PyExc_TypeError, "script must be an integer or string");
		return nullptr;
	}

	int acsArguments[4] = { 0, 0, 0, 0 };
	int argumentCount = 0;
	if (argumentsObject != Py_None)
	{
		PyObject* sequence = PySequence_Fast(argumentsObject, "arguments must be a sequence of at most four integers");
		if (sequence == nullptr) return nullptr;
		const Py_ssize_t sequenceSize = PySequence_Fast_GET_SIZE(sequence);
		if (sequenceSize > 4)
		{
			Py_DECREF(sequence);
			PyErr_SetString(PyExc_ValueError, "ACS accepts at most four arguments");
			return nullptr;
		}
		argumentCount = static_cast<int>(sequenceSize);
		for (int i = 0; i < argumentCount; ++i)
		{
			const long number = PyLong_AsLong(PySequence_Fast_GET_ITEM(sequence, i));
			if (PyErr_Occurred())
			{
				Py_DECREF(sequence);
				return nullptr;
			}
			if (number < INT_MIN || number > INT_MAX)
			{
				Py_DECREF(sequence);
				PyErr_SetString(PyExc_OverflowError, "ACS argument is outside the engine's 32-bit range");
				return nullptr;
			}
			acsArguments[i] = static_cast<int>(number);
		}
		Py_DECREF(sequence);
	}

	int flags = always ? ACS_ALWAYS : 0;
	if (wantResult) flags |= ACS_ALWAYS | ACS_WANTRESULT;
	AActor* activator = (consoleplayer >= 0 && static_cast<unsigned>(consoleplayer) < MAXPLAYERS)
		? players[consoleplayer].mo : nullptr;
	const int result = P_StartScript(primaryLevel, activator, nullptr, script,
		primaryLevel->MapName.GetChars(), acsArguments, argumentCount, flags);
	return wantResult ? PyLong_FromLong(result) : PyBool_FromLong(result != 0);
}

PyObject* PyBdReadText(PyObject*, PyObject* args)
{
	if (!CheckEngineThread()) return nullptr;
	const char* path = nullptr;
	if (!PyArg_ParseTuple(args, "s:read_text", &path)) return nullptr;
	if (currentContainer < 0)
	{
		PyErr_SetString(PyExc_RuntimeError, "read_text must be called while a mod script or callback is executing");
		return nullptr;
	}
	std::string source;
	if (!ReadResourceText(currentContainer, path, source, false))
	{
		PyErr_Format(PyExc_FileNotFoundError, "resource '%s' was not found in the current mod", path);
		return nullptr;
	}
	return PyUnicode_DecodeUTF8(source.data(), static_cast<Py_ssize_t>(source.size()), "strict");
}

PyObject* ExecuteResourceModule(int container, const std::string& path, const std::string& moduleName, bool registerNamed);

PyObject* PyBdImportScript(PyObject*, PyObject* args, PyObject* kwargs)
{
	if (!CheckEngineThread()) return nullptr;
	const char* path = nullptr;
	const char* requestedName = nullptr;
	static const char* keywords[] = { "path", "module_name", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|z:import_script", const_cast<char**>(keywords),
		&path, &requestedName)) return nullptr;
	if (currentContainer < 0)
	{
		PyErr_SetString(PyExc_RuntimeError, "import_script must be called while a mod script or callback is executing");
		return nullptr;
	}
	std::string moduleName = requestedName == nullptr ? "biaseddoom_vfs_helper" : requestedName;
	return ExecuteResourceModule(currentContainer, path, moduleName, false);
}

PyObject* PyBdSchedule(PyObject*, PyObject* args, PyObject* kwargs)
{
	if (!CheckEngineThread()) return nullptr;
	PyObject* callable = nullptr;
	unsigned long long delay = 1;
	unsigned long long repeat = 0;
	int mapLocal = 1;
	static const char* keywords[] = { "callback", "delay", "repeat", "map_local", nullptr };
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|KKp:schedule", const_cast<char**>(keywords),
		&callable, &delay, &repeat, &mapLocal)) return nullptr;
	if (!PyCallable_Check(callable))
	{
		PyErr_SetString(PyExc_TypeError, "scheduled callback must be callable");
		return nullptr;
	}
	const int taskLimit = std::clamp(static_cast<int>(py_max_tasks), 1, 100000);
	if (static_cast<int>(scheduledTasks.size()) >= taskLimit)
	{
		PyErr_Format(PyExc_RuntimeError, "Python scheduled-task limit (%d) reached", taskLimit);
		return nullptr;
	}
	if (delay > 0x7fffffffffffffffULL || repeat > 0x7fffffffffffffffULL)
	{
		PyErr_SetString(PyExc_OverflowError, "task delays must fit in a signed 63-bit tic count");
		return nullptr;
	}
	if (nextTaskId == 0) ++nextTaskId;
	ScheduledTask task;
	task.Id = nextTaskId++;
	task.DueTick = taskClock + delay;
	task.Interval = repeat;
	task.Callable = Py_NewRef(callable);
	task.Container = currentContainer;
	task.Source = currentSource;
	task.MapLocal = mapLocal != 0;
	const bool levelActive = primaryLevel != nullptr && primaryLevel->MapName.IsNotEmpty();
	task.MapSerial = mapSerial + (task.MapLocal && !levelActive ? 1 : 0);
	const uint64_t id = task.Id;
	scheduledTasks.push_back(std::move(task));
	return PyLong_FromUnsignedLongLong(id);
}

PyObject* PyBdCancelTask(PyObject*, PyObject* args)
{
	if (!CheckEngineThread()) return nullptr;
	unsigned long long id = 0;
	if (!PyArg_ParseTuple(args, "K:cancel_task", &id)) return nullptr;
	for (ScheduledTask& task : scheduledTasks)
	{
		if (task.Id != id || task.Cancelled) continue;
		task.Cancelled = true;
		CleanupScheduledTasks();
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

PyObject* PyBdTaskCount(PyObject*, PyObject*)
{
	if (!CheckEngineThread()) return nullptr;
	Py_ssize_t count = 0;
	for (const ScheduledTask& task : scheduledTasks)
	{
		if (!task.Cancelled) ++count;
	}
	return PyLong_FromSsize_t(count);
}

PyObject* PyBdProfile(PyObject*, PyObject*)
{
	if (!CheckEngineThread()) return nullptr;
	PyObject* result = PyDict_New();
	PyObject* entries = PyList_New(0);
	if (result == nullptr || entries == nullptr)
	{
		Py_XDECREF(result);
		Py_XDECREF(entries);
		return nullptr;
	}
	for (const Callback& callback : callbacks)
	{
		PyObject* entry = Py_BuildValue("{s:s,s:s,s:K,s:K,s:K,s:K,s:K,s:I,s:i,s:i,s:i}",
			"event", callback.Event.c_str(),
			"source", callback.Source.c_str(),
			"calls", callback.Calls,
			"total_us", callback.TotalMicroseconds,
			"max_us", callback.MaximumMicroseconds,
			"budget_skips", callback.BudgetSkips,
			"budget_overruns", callback.BudgetOverruns,
			"every", callback.Every,
			"priority", callback.Priority,
			"failed", callback.Failed ? 1 : 0,
			"budget_disabled", callback.BudgetDisabled ? 1 : 0);
		if (entry == nullptr || PyList_Append(entries, entry) < 0)
		{
			Py_XDECREF(entry);
			Py_DECREF(entries);
			Py_DECREF(result);
			return nullptr;
		}
		Py_DECREF(entry);
	}
	PyDict_SetItemString(result, "callbacks", entries);
	Py_DECREF(entries);
	PyObject* tasks = PyList_New(0);
	if (tasks == nullptr) { Py_DECREF(result); return nullptr; }
	for (const ScheduledTask& task : scheduledTasks)
	{
		if (task.Cancelled) continue;
		PyObject* entry = Py_BuildValue("{s:K,s:s,s:K,s:K,s:K,s:K,s:K,s:K,s:K,s:O}",
			"id", task.Id,
			"source", task.Source.c_str(),
			"due_tick", task.DueTick,
			"repeat", task.Interval,
			"calls", task.Calls,
			"total_us", task.TotalMicroseconds,
			"max_us", task.MaximumMicroseconds,
			"budget_skips", task.BudgetSkips,
			"budget_overruns", task.BudgetOverruns,
			"map_local", task.MapLocal ? Py_True : Py_False);
		if (entry == nullptr || PyList_Append(tasks, entry) < 0)
		{
			Py_XDECREF(entry); Py_DECREF(tasks); Py_DECREF(result); return nullptr;
		}
		Py_DECREF(entry);
	}
	PyDict_SetItemString(result, "tasks", tasks);
	Py_DECREF(tasks);
	PyObject* budget = PyLong_FromLong(static_cast<int>(py_tick_budget_ms));
	PyObject* hard = PyBool_FromLong(py_tick_hard_budget ? 1 : 0);
	PyObject* overrunLimit = PyLong_FromLong(static_cast<int>(py_tick_overrun_limit));
	PyObject* overruns = PyLong_FromUnsignedLongLong(tickBudgetOverruns);
	PyObject* skips = PyLong_FromUnsignedLongLong(tickBudgetSkips);
	if (budget != nullptr) { PyDict_SetItemString(result, "tick_budget_ms", budget); Py_DECREF(budget); }
	if (hard != nullptr) { PyDict_SetItemString(result, "hard_budget", hard); Py_DECREF(hard); }
	if (overrunLimit != nullptr) { PyDict_SetItemString(result, "overrun_limit", overrunLimit); Py_DECREF(overrunLimit); }
	if (overruns != nullptr) { PyDict_SetItemString(result, "budget_overruns", overruns); Py_DECREF(overruns); }
	if (skips != nullptr) { PyDict_SetItemString(result, "budget_skips", skips); Py_DECREF(skips); }
	return result;
}

PyObject* PyBdResetProfile(PyObject*, PyObject*)
{
	if (!CheckEngineThread()) return nullptr;
	for (Callback& callback : callbacks)
	{
		callback.Calls = 0;
		callback.TotalMicroseconds = 0;
		callback.MaximumMicroseconds = 0;
		callback.BudgetSkips = 0;
		callback.BudgetOverruns = 0;
		callback.BudgetWarnings = 0;
		callback.ConsecutiveOverruns = 0;
	}
	for (ScheduledTask& task : scheduledTasks)
	{
		task.Calls = 0;
		task.TotalMicroseconds = 0;
		task.MaximumMicroseconds = 0;
		task.BudgetSkips = 0;
		task.BudgetOverruns = 0;
		task.ConsecutiveOverruns = 0;
	}
	tickBudgetOverruns = 0;
	tickBudgetSkips = 0;
	Py_RETURN_NONE;
}

PyMethodDef EngineMethods[] = {
	{ "log", BD_PY_KEYWORD_FUNCTION(PyBdLog), METH_VARARGS | METH_KEYWORDS, "log(message, level='info') -> None" },
	{ "_write_output", PyBdWriteOutput, METH_VARARGS, nullptr },
	{ "_register_callback", BD_PY_KEYWORD_FUNCTION(PyBdRegisterCallback), METH_VARARGS | METH_KEYWORDS, nullptr },
	{ "current_map", PyBdCurrentMap, METH_NOARGS, "Return the current map lump name or None." },
	{ "level_time", PyBdLevelTime, METH_NOARGS, "Return elapsed level time in 35 Hz tics." },
	{ "players", PyBdPlayers, METH_NOARGS, "Return snapshots of all active players." },
	{ "actors", BD_PY_KEYWORD_FUNCTION(PyBdActors), METH_VARARGS | METH_KEYWORDS, "Return actor snapshots, optionally filtered by class or TID." },
	{ "_actor_class_info", PyBdActorClassInfo, METH_NOARGS, nullptr },
	{ "actor", PyBdActor, METH_VARARGS, "Return the first actor snapshot for a TID, or None." },
	{ "spawn_actor", BD_PY_KEYWORD_FUNCTION(PyBdSpawnActor), METH_VARARGS | METH_KEYWORDS, "Spawn an actor and return its snapshot." },
	{ "damage_actor", BD_PY_KEYWORD_FUNCTION(PyBdDamageActor), METH_VARARGS | METH_KEYWORDS, "Damage the first actor with a TID." },
	{ "set_actor_velocity", PyBdSetActorVelocity, METH_VARARGS, "Set actor velocity by TID." },
	{ "destroy_actor", PyBdDestroyActor, METH_VARARGS, "Destroy the first actor with a TID." },
	{ "get_cvar", PyBdGetCVar, METH_VARARGS, "Read a console variable using its native Python type." },
	{ "set_cvar", PyBdSetCVar, METH_VARARGS, "Set a console variable and return the applied value." },
	{ "execute", PyBdExecute, METH_VARARGS, "Queue an engine console command." },
	{ "execute_acs", BD_PY_KEYWORD_FUNCTION(PyBdExecuteACS), METH_VARARGS | METH_KEYWORDS, "Start a numeric or named ACS script." },
	{ "read_text", PyBdReadText, METH_VARARGS, "Read a UTF-8 resource from the current mod." },
	{ "import_script", BD_PY_KEYWORD_FUNCTION(PyBdImportScript), METH_VARARGS | METH_KEYWORDS, "Execute and return another Python module from the current mod." },
	{ "profile", PyBdProfile, METH_NOARGS, "Return per-callback timing and budget statistics." },
	{ "reset_profile", PyBdResetProfile, METH_NOARGS, "Reset callback timing and budget statistics." },
	{ "schedule", BD_PY_KEYWORD_FUNCTION(PyBdSchedule), METH_VARARGS | METH_KEYWORDS, "Schedule a one-shot or repeating callable in engine tics." },
	{ "cancel_task", PyBdCancelTask, METH_VARARGS, "Cancel a scheduled task by ID." },
	{ "task_count", PyBdTaskCount, METH_NOARGS, "Return the number of active scheduled tasks." },
	{ nullptr, nullptr, 0, nullptr },
};

PyModuleDef EngineModuleDefinition = {
	PyModuleDef_HEAD_INIT,
	"biaseddoom",
	"Trusted embedded scripting API for BiasedDoom mods.",
	-1,
	EngineMethods,
};

PyMODINIT_FUNC PyInit_biaseddoom()
{
	PyObject* module = PyModule_Create(&EngineModuleDefinition);
	if (module == nullptr) return nullptr;
	if (!GameApi::Initialize(module))
	{
		Py_DECREF(module);
		return nullptr;
	}
	PyModule_AddIntConstant(module, "API_VERSION", PythonApiVersion);
	PyModule_AddIntConstant(module, "TICRATE", TICRATE);
	PyModule_AddStringConstant(module, "RUNTIME", "CPython");
	return module;
}

const char* BootstrapSource = R"PY(
import sys as _sys

def on(event_name, *, every=1, priority=0, class_name=None, tid=0, player=-1):
    """Decorator registering a callback for a BiasedDoom lifecycle event."""
    def decorate(callback):
        _register_callback(
            event_name,
            callback,
            every=every,
            priority=priority,
            class_name=class_name,
            tid=tid,
            player=player,
        )
        return callback
    return decorate

class _EngineWriter:
    def __init__(self, stream):
        self.stream = stream

    def write(self, text):
        _write_output(str(text), self.stream, 0)
        return len(text)

    def flush(self):
        _write_output("", self.stream, 1)

    def isatty(self):
        return False

_sys.stdout = _EngineWriter(0)
_sys.stderr = _EngineWriter(1)

import random as _random

def _actor_const_name(class_name):
    # "DoomImp" -> "DOOM_IMP", "MBFHelperDog" -> "MBF_HELPER_DOG"
    out = []
    for i, ch in enumerate(class_name):
        if not ch.isalnum():
            out.append('_')
            continue
        if ch.isupper() and i > 0 and (class_name[i - 1].islower() or class_name[i - 1].isdigit()
                                       or (i + 1 < len(class_name) and class_name[i + 1].islower())):
            out.append('_')
        out.append(ch.upper())
    name = ''.join(out)
    if name and name[0].isdigit():
        name = '_' + name
    return name

class _ActorsRegistry:
    """Actor class registry: named constants, discovery, random spawns.

    Attribute access maps UPPER_SNAKE constants to engine class names:
        actors.DOOM_IMP -> "DoomImp"
    The registry stays callable, so actors(...) still queries live actors.
    """

    def __init__(self, query):
        self._query = query
        self._loaded = False
        self._info = {}      # class name -> (parent name, kind mask)
        self._by_const = {}  # CONST name -> class name
        self._by_class = {}  # class name -> CONST name

    def __call__(self, *args, **kwargs):
        return self._query(*args, **kwargs)

    def _load(self):
        if self._loaded:
            return
        self._loaded = True
        for class_name, parent, kind in _actor_class_info():
            self._info[class_name] = (parent, kind)
            const = _actor_const_name(class_name)
            if const not in self._by_const:
                self._by_const[const] = class_name
                self._by_class[class_name] = const

    def __getattr__(self, name):
        if name.startswith('_'):
            raise AttributeError(name)
        self._load()
        try:
            return self._by_const[name]
        except KeyError:
            raise AttributeError(
                f"biaseddoom.actors has no constant {name!r}; "
                "use actors.names() or dir(actors) to list available actor classes") from None

    def __dir__(self):
        self._load()
        return sorted(self._by_const)

    def names(self):
        """All registered actor class names, sorted (e.g. \"DoomImp\")."""
        self._load()
        return sorted(self._info)

    def constants(self):
        """All constant names, sorted (e.g. \"DOOM_IMP\")."""
        self._load()
        return sorted(self._by_const)

    def resolve(self, name):
        """Accept a CONST name or an engine class name; return the class name or None."""
        self._load()
        if name in self._info:
            return name
        return self._by_const.get(name)

    def children_of(self, parent):
        """Sorted class names descending from parent (inclusive);
        parent may be a CONST name or an engine class name."""
        self._load()
        root = self.resolve(parent)
        if root is None:
            raise ValueError(f"unknown actor class {parent!r}")
        result = []
        for class_name in self._info:
            node = class_name
            while node:
                if node == root:
                    result.append(class_name)
                    break
                node = self._info.get(node, (None, 0))[0]
        return sorted(result)

    def _kind_names(self, mask):
        self._load()
        return sorted(n for n, (_, kind) in self._info.items() if kind & mask)

    def monsters(self):
        """Class names of shootable, kill-counted actors."""
        return self._kind_names(1)

    def projectiles(self):
        """Class names of missile actors."""
        return self._kind_names(2)

    def weapons(self):
        """Class names descending from Weapon."""
        return self._kind_names(4)

    def items(self):
        """Class names descending from Inventory."""
        return self._kind_names(8)

    def players(self):
        """Class names descending from PlayerPawn."""
        return self._kind_names(16)

    def random(self, kind=None):
        """Return a random actor class name. kind may be None (any actor),
        a category (\"monsters\", \"projectiles\", \"weapons\", \"items\",
        \"players\"), or a class/CONST name to pick among its descendants."""
        if kind is None:
            pool = self.names()
        else:
            mask = _ACTOR_KINDS.get(str(kind).lower())
            pool = self._kind_names(mask) if mask is not None else self.children_of(kind)
        if not pool:
            raise ValueError(f"no actor classes match {kind!r}")
        return _random.choice(pool)

    def spawn_random(self, x, y, z, kind="monsters", **kwargs):
        """Spawn a random actor of the given category at (x, y, z)."""
        return spawn_actor(self.random(kind), x, y, z, **kwargs)

_ACTOR_KINDS = {
    "monster": 1, "monsters": 1,
    "projectile": 2, "projectiles": 2,
    "weapon": 4, "weapons": 4,
    "item": 8, "items": 8, "inventory": 8,
    "player": 16, "players": 16,
}

_actors_query = actors
actors = _ActorsRegistry(_actors_query)

_STUB_BEGIN = "    # @@GENERATED ACTOR CONSTANTS BEGIN@@"
_STUB_END = "    # @@GENERATED ACTOR CONSTANTS END@@"

def _stub_constants_block():
    lines = [_STUB_BEGIN]
    for const in actors.constants():
        lines.append(f"    {const}: str  # {actors.resolve(const)}")
    lines.append(_STUB_END)
    return "\n".join(lines)

def _public_api_names(mod):
    names = []
    for name in sorted(dir(mod)):
        if name.startswith('_'):
            continue
        value = getattr(mod, name)
        if isinstance(value, type(_sys)):
            continue
        names.append(name)
    return names

def _stub_skeleton():
    mod = _sys.modules["biaseddoom"]
    out = ['"""Type stubs for the embedded biaseddoom module (generated by dumppystub)."""',
           "from typing import Any, Callable, Optional, Union", ""]
    for name in _public_api_names(mod):
        if name == "actors":
            continue
        value = getattr(mod, name)
        if isinstance(value, bool):
            continue
        if isinstance(value, int):
            out.append(f"{name}: int")
        elif isinstance(value, str):
            out.append(f"{name}: str")
        elif isinstance(value, dict):
            out.append(f"{name}: dict")
    out.append("")
    for tname in ("Actor", "Line", "Sector", "Player"):
        t = getattr(mod, tname, None)
        if not isinstance(t, type):
            continue
        out.append(f"class {tname}:")
        for mname in sorted(dir(t)):
            if mname.startswith("__"):
                continue
            member = getattr(t, mname, None)
            doc = (getattr(member, "__doc__", None) or "").strip().splitlines()
            comment = f"  # {doc[0]}" if doc else ""
            if callable(member):
                out.append(f"    def {mname}(self, *args, **kwargs): ...{comment}")
            else:
                out.append(f"    {mname}: Any{comment}")
        out.append("")
    for name in _public_api_names(mod):
        value = getattr(mod, name)
        if not callable(value) or isinstance(value, type) or name == "actors":
            continue
        doc = (getattr(value, "__doc__", None) or "").strip().splitlines()
        comment = f"  # {doc[0]}" if doc else ""
        out.append(f"def {name}(*args, **kwargs): ...{comment}")
    out.append("")
    out.append("class _ActorsRegistry:")
    out.append("    def __call__(self, *args, **kwargs): ...")
    for mname in ("names", "constants", "resolve", "children_of", "monsters",
                  "projectiles", "weapons", "items", "players", "random", "spawn_random"):
        out.append(f"    def {mname}(self, *args, **kwargs): ...")
    out.append("    def __getattr__(self, name: str) -> str: ...")
    out.append(_stub_constants_block())
    out.append("")
    out.append("actors: _ActorsRegistry")
    out.append("")
    return "\n".join(out)

def _dump_stub(path):
    """Regenerate the .pyi stub's actor constants block (or a full skeleton)."""
    if not path:
        path = "biaseddoom.pyi"
    block = _stub_constants_block()
    try:
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError:
        text = None
    if text is None:
        text = _stub_skeleton()
        action = "created"
    elif _STUB_BEGIN in text and _STUB_END in text:
        start = text.index(_STUB_BEGIN)
        end = text.index(_STUB_END) + len(_STUB_END)
        text = text[:start] + block + text[end:]
        action = "updated"
    else:
        raise RuntimeError(f"{path}: no generated-constants markers found; refusing to overwrite")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    missing = []
    mod = _sys.modules["biaseddoom"]
    for name in _public_api_names(mod):
        if name == "actors":
            continue
        if f"def {name}(" in text or f"{name}:" in text or f"class {name}" in text:
            continue
        missing.append(name)
    message = f"stub {action}: {path} ({len(actors.constants())} actor constants)"
    if missing:
        message += "\nWARNING: public API missing from stub: " + ", ".join(missing)
    return message
)PY";

void RegisterNamedCallbacks(PyObject* module, int container, const std::string& source)
{
	static const std::pair<const char*, const char*> callbackNames[] = {
		{ "on_engine_start", "engine_start" },
		{ "on_map_load", "map_load" },
		{ "on_map_unload", "map_unload" },
		{ "on_pre_tick", "pre_tick" },
		{ "on_tick", "tick" },
		{ "on_post_tick", "post_tick" },
		{ "on_actor_spawned", "actor_spawned" },
		{ "on_actor_died", "actor_died" },
		{ "on_actor_damaged", "actor_damaged" },
		{ "on_actor_destroyed", "actor_destroyed" },
		{ "on_actor_revived", "actor_revived" },
		{ "on_line_activated", "line_activated" },
		{ "on_line_activation_failed", "line_activation_failed" },
		{ "on_player_entered", "player_entered" },
		{ "on_player_spawned", "player_spawned" },
		{ "on_player_respawned", "player_respawned" },
		{ "on_player_died", "player_died" },
		{ "on_player_disconnected", "player_disconnected" },
		{ "on_save", "save" },
		{ "on_load", "load" },
		{ "on_engine_shutdown", "engine_shutdown" },
	};

	for (const auto& names : callbackNames)
	{
		PyObject* callable = PyObject_GetAttrString(module, names.first);
		if (callable == nullptr)
		{
			PyErr_Clear();
			continue;
		}
		if (PyCallable_Check(callable)) RegisterCallback(names.second, callable, container, source);
		else Printf(TEXTCOLOR_YELLOW "Python value %s in %s is not callable and was ignored.\n", names.first, source.c_str());
		Py_DECREF(callable);
	}
}

PyObject* ExecuteResourceModule(int container, const std::string& path, const std::string& moduleName, bool registerNamed)
{
	std::string source;
	if (!ReadResourceText(container, path, source, true))
	{
		PyErr_Format(PyExc_FileNotFoundError, "resource '%s' was not found in the current mod", path.c_str());
		return nullptr;
	}
	if (source.find('\0') != std::string::npos)
	{
		PyErr_Format(PyExc_SyntaxError, "Python resource '%s' contains an embedded NUL byte", path.c_str());
		return nullptr;
	}

	PyObject* module = PyModule_New(moduleName.c_str());
	if (module == nullptr) return nullptr;
	PyObject* dictionary = PyModule_GetDict(module);
	PyObject* fileName = PyUnicode_FromFormat("vfs://%s", path.c_str());
	if (fileName != nullptr)
	{
		PyDict_SetItemString(dictionary, "__file__", fileName);
		Py_DECREF(fileName);
	}
	PyDict_SetItemString(dictionary, "__builtins__", PyEval_GetBuiltins());

	const int previousContainer = currentContainer;
	const std::string previousSource = currentSource;
	const size_t callbackStart = callbacks.size();
	currentContainer = container;
	currentSource = path;
	PyObject* code = Py_CompileString(source.c_str(), path.c_str(), Py_file_input);
	PyObject* result = code == nullptr ? nullptr : PyEval_EvalCode(code, dictionary, dictionary);
	Py_XDECREF(code);
	currentContainer = previousContainer;
	currentSource = previousSource;
	if (result == nullptr)
	{
		// Decorators execute immediately. A later top-level exception must not
		// leave callbacks from a module that the loader reports as skipped.
		for (size_t index = callbackStart; index < callbacks.size(); ++index)
		{
			Py_XDECREF(callbacks[index].Callable);
		}
		callbacks.resize(callbackStart);
		RebuildEventPresence();
		Py_DECREF(module);
		return nullptr;
	}
	Py_DECREF(result);
	if (registerNamed) RegisterNamedCallbacks(module, container, path);
	return module;
}

bool InitializeInterpreter()
{
	if (!inittabRegistered)
	{
		if (PyImport_AppendInittab("biaseddoom", &PyInit_biaseddoom) == -1)
		{
			Printf(TEXTCOLOR_RED "Could not register the BiasedDoom Python module.\n");
			return false;
		}
		inittabRegistered = true;
	}

	PyConfig config;
	PyConfig_InitIsolatedConfig(&config);
	config.install_signal_handlers = 0;
	config.parse_argv = 0;
	config.site_import = 0;
	config.user_site_directory = 0;
	config.write_bytecode = 0;
	PyStatus status = PyConfig_SetBytesString(&config, &config.program_name, GAMENAMELOWERCASE);

	FString pythonHome = progdir;
	if (pythonHome.IsNotEmpty() && pythonHome.Back() != '/' && pythonHome.Back() != '\\')
	{
		pythonHome << '/';
	}
	pythonHome << "python";
	FString encodings = pythonHome;
#ifdef _WIN32
	encodings << "/Lib/encodings/__init__.py";
#else
	encodings.AppendFormat("/lib/python%d.%d/encodings/__init__.py", PY_MAJOR_VERSION, PY_MINOR_VERSION);
#endif
	if (!PyStatus_Exception(status) && !FileExists(encodings))
	{
		Printf(TEXTCOLOR_RED "Could not initialize CPython: private standard library is missing (%s).\n",
			encodings.GetChars());
		PyConfig_Clear(&config);
		return false;
	}
	if (!PyStatus_Exception(status))
	{
		status = PyConfig_SetBytesString(&config, &config.home, pythonHome.GetChars());
	}

	if (!PyStatus_Exception(status)) status = Py_InitializeFromConfig(&config);
	if (PyStatus_Exception(status))
	{
		Printf(TEXTCOLOR_RED "Could not initialize CPython: %s\n",
			status.err_msg == nullptr ? "unknown initialization error" : status.err_msg);
		PyConfig_Clear(&config);
		return false;
	}
	PyConfig_Clear(&config);

	engineModule = PyImport_ImportModule("biaseddoom");
	if (engineModule == nullptr)
	{
		ReportPythonError("module initialization", "biaseddoom");
		Py_FinalizeEx();
		return false;
	}

	stateDictionary = PyDict_New();
	PyObject* moduleStateReference = stateDictionary == nullptr ? nullptr : Py_NewRef(stateDictionary);
	if (stateDictionary == nullptr || PyModule_AddObject(engineModule, "state", moduleStateReference) < 0)
	{
		Py_XDECREF(moduleStateReference);
		Py_XDECREF(stateDictionary);
		stateDictionary = nullptr;
		ReportPythonError("state initialization", "biaseddoom");
		Py_DECREF(engineModule);
		engineModule = nullptr;
		Py_FinalizeEx();
		return false;
	}
	PyObject* bootstrapResult = PyRun_String(BootstrapSource, Py_file_input,
		PyModule_GetDict(engineModule), PyModule_GetDict(engineModule));
	if (bootstrapResult == nullptr)
	{
		ReportPythonError("bootstrap", "biaseddoom");
		Py_DECREF(stateDictionary);
		stateDictionary = nullptr;
		Py_DECREF(engineModule);
		engineModule = nullptr;
		Py_FinalizeEx();
		return false;
	}
	Py_DECREF(bootstrapResult);
	return true;
}

std::string DumpStateJson()
{
	if (!active || stateDictionary == nullptr) return {};
	if (!ValidateStateDictionary())
	{
		ReportPythonError("state serialization", "biaseddoom.state");
		return {};
	}
	PyObject* json = PyImport_ImportModule("json");
	if (json == nullptr)
	{
		ReportPythonError("state serialization", "import json");
		return {};
	}
	PyObject* dumps = PyObject_GetAttrString(json, "dumps");
	PyObject* kwargs = Py_BuildValue("{s:O,s:O,s:O}",
		"sort_keys", Py_True, "ensure_ascii", Py_False, "allow_nan", Py_False);
	PyObject* args = PyTuple_Pack(1, stateDictionary);
	PyObject* encoded = dumps == nullptr ? nullptr : PyObject_Call(dumps, args, kwargs);
	std::string result;
	if (encoded != nullptr)
	{
		const char* text = PyUnicode_AsUTF8(encoded);
		if (text != nullptr) result = text;
	}
	else ReportPythonError("state serialization", "biaseddoom.state");
	Py_XDECREF(encoded);
	Py_DECREF(args);
	Py_DECREF(kwargs);
	Py_XDECREF(dumps);
	Py_DECREF(json);
	return result;
}

bool LoadStateJson(const std::string& encoded)
{
	if (!active || stateDictionary == nullptr || encoded.empty()) return false;
	if (!ValidateStateDictionary())
	{
		ReportPythonError("state restoration", "biaseddoom.state");
		return false;
	}
	PyObject* json = PyImport_ImportModule("json");
	PyObject* loads = json == nullptr ? nullptr : PyObject_GetAttrString(json, "loads");
	PyObject* text = PyUnicode_FromStringAndSize(encoded.data(), static_cast<Py_ssize_t>(encoded.size()));
	PyObject* decoded = loads == nullptr || text == nullptr ? nullptr : PyObject_CallFunctionObjArgs(loads, text, nullptr);
	bool success = false;
	if (decoded != nullptr && PyDict_Check(decoded))
	{
		PyDict_Clear(stateDictionary);
		success = PyDict_Update(stateDictionary, decoded) == 0;
	}
	else if (decoded != nullptr)
	{
		PyErr_SetString(PyExc_TypeError, "saved biaseddoom.state must decode to a dictionary");
	}
	if (!success) ReportPythonError("state restoration", "biaseddoom.state");
	Py_XDECREF(decoded);
	Py_XDECREF(text);
	Py_XDECREF(loads);
	Py_XDECREF(json);
	return success;
}

} // namespace

bool CheckApiThread()
{
	return CheckEngineThread();
}

bool CheckGameplayMutation()
{
	return CheckMutationAllowed();
}

unsigned int GetErrorCount()
{
	return s_pythonErrorCount;
}

void SetErrorLogPath(const char* path)
{
	s_pythonErrorLogPath = path != nullptr ? path : "";
}

void DumpStub(const char* path)
{
	if (!IsActive())
	{
		Printf("Python is not active (start the game with -python and a script).\n");
		return;
	}
	PyObject* func = PyObject_GetAttrString(engineModule, "_dump_stub");
	if (func == nullptr)
	{
		PyErr_Clear();
		Printf("stub generator unavailable\n");
		return;
	}
	PyObject* result = path != nullptr
		? PyObject_CallFunction(func, "s", path)
		: PyObject_CallFunction(func, "O", Py_None);
	Py_DECREF(func);
	if (result == nullptr)
	{
		ReportPythonError("dumppystub", "");
		return;
	}
	Printf("%s\n", PyString(result).c_str());
	Py_DECREF(result);
}

bool CheckSessionMutation()
{
	return CheckSessionMutationAllowed();
}

bool IsCompiled()
{
	return true;
}

bool IsActive()
{
	return active;
}

bool Initialize()
{
	if (active) return true;
	engineThread = std::this_thread::get_id();
	DiscoverScripts();
	if (discoveredScripts.empty()) return false;
	if (!RuntimeRequested())
	{
		Printf(TEXTCOLOR_YELLOW "%zu Python script%s found but not executed. Python mods are trusted code; use -python or set py_enabled true to opt in.\n",
			discoveredScripts.size(), discoveredScripts.size() == 1 ? " was" : "s were");
		return false;
	}

	if (!InitializeInterpreter()) return false;
	active = true;
	unsigned loaded = 0;
	for (size_t index = 0; index < discoveredScripts.size(); ++index)
	{
		const ScriptEntry& entry = discoveredScripts[index];
		const std::string moduleName = "biaseddoom_mod_" + std::to_string(entry.Container) + "_" + std::to_string(index);
		PyObject* module = ExecuteResourceModule(entry.Container, entry.Path, moduleName, true);
		if (module == nullptr)
		{
			ReportPythonError("script load", entry.Resource + ":" + entry.Path);
			continue;
		}
		modules.push_back({ module, entry.Container, entry.Path });
		++loaded;
	}

	Printf("Python: CPython %s initialized; loaded %u/%zu script%s.\n",
		Py_GetVersion(), loaded, discoveredScripts.size(), discoveredScripts.size() == 1 ? "" : "s");
	InvokeEvent("engine_start", BuildEvent("engine_start"));
	return true;
}

void Shutdown()
{
	if (!active) return;
	InvokeEvent("engine_shutdown", BuildEvent("engine_shutdown"));
	EmitBufferedOutput(stdoutBuffer, nullptr, true, false);
	EmitBufferedOutput(stderrBuffer, nullptr, true, true);
	// Prevent atexit hooks and object finalizers from calling back into engine
	// state or appending new callbacks while interpreter-owned references are
	// being released. Detach the writers so CPython's final flush is inert.
	if (PySys_SetObject("stdout", Py_None) < 0) PyErr_Clear();
	if (PySys_SetObject("stderr", Py_None) < 0) PyErr_Clear();
	active = false;
	for (Callback& callback : callbacks) Py_XDECREF(callback.Callable);
	callbacks.clear();
	eventHasCallbacks.fill(false);
	for (ScheduledTask& task : scheduledTasks) Py_XDECREF(task.Callable);
	scheduledTasks.clear();
	callbacksNeedSort = false;
	callbackDispatchDepth = 0;
	tickBudgetMicroseconds = 0;
	tickBudgetOverruns = 0;
	tickBudgetSkips = 0;
	taskClock = 0;
	nextTaskId = 1;
	mapSerial = 0;
	taskDispatchDepth = 0;
	for (ScriptModule& module : modules) Py_XDECREF(module.Module);
	modules.clear();
	Py_CLEAR(stateDictionary);
	Py_CLEAR(engineModule);
	GameApi::Shutdown();
	loadCallbackPending = false;
	currentContainer = -1;
	currentSource.clear();
	Py_FinalizeEx();
}

bool Reload()
{
	std::string state = DumpStateJson();
	const bool hadLevel = primaryLevel != nullptr && primaryLevel->MapName.IsNotEmpty();
	Shutdown();
	if (!Initialize()) return false;
	if (!state.empty()) LoadStateJson(state);
	if (hadLevel) OnWorldLoaded();
	return true;
}

void OnWorldLoaded()
{
	++mapSerial;
	if (!HasCallbacks("map_load")) return;
	PyObject* event = BuildEvent("map_load");
	DictSetBool(event, "from_savegame", savegamerestore);
	InvokeEvent("map_load", event);
}

void OnWorldUnloaded(const char* nextMap)
{
	if (!HasCallbacks("map_unload"))
	{
		CancelMapLocalTasks();
		GameApi::InvalidateWorld();
		return;
	}
	PyObject* event = BuildEvent("map_unload");
	if (nextMap == nullptr || *nextMap == 0) DictSet(event, "next_map", Py_NewRef(Py_None));
	else DictSetString(event, "next_map", nextMap);
	const bool wasBlocked = gameplayMutationBlocked;
	gameplayMutationBlocked = true;
	InvokeEvent("map_unload", event);
	gameplayMutationBlocked = wasBlocked;
	CancelMapLocalTasks();
	GameApi::InvalidateWorld();
}

void OnWorldPreTick()
{
	if (!active) return;
	tickBudgetMicroseconds = 0;
	++taskClock;
	ProcessScheduledTasks();
	if (!HasCallbacks("pre_tick")) return;
	PyObject* event = BuildEvent("pre_tick");
	DictSetBool(event, "paused", paused != 0);
	InvokeEvent("pre_tick", event);
}

void OnWorldTick()
{
	if (!HasCallbacks("tick")) return;
	PyObject* event = BuildEvent("tick");
	DictSetBool(event, "paused", paused != 0);
	InvokeEvent("tick", event);
}

void OnWorldPostTick()
{
	if (!HasCallbacks("post_tick")) return;
	PyObject* event = BuildEvent("post_tick");
	DictSetBool(event, "paused", paused != 0);
	DictSetInt(event, "python_time_us", tickBudgetMicroseconds);
	InvokeEvent("post_tick", event);
}

void OnActorSpawned(AActor* actor)
{
	if (!HasCallbacks("actor_spawned")) return;
	PyObject* event = BuildEvent("actor_spawned");
	DictSet(event, "actor", ActorSnapshot(actor));
	DictSet(event, "actor_ref", GameApi::MakeActorRef(actor));
	InvokeEvent("actor_spawned", event, actor, ActorPlayerNumber(actor));
}

void OnActorDied(AActor* actor, AActor* inflictor)
{
	if (!HasCallbacks("actor_died")) return;
	PyObject* event = BuildEvent("actor_died");
	DictSet(event, "actor", ActorSnapshot(actor));
	DictSet(event, "inflictor", ActorSnapshot(inflictor));
	DictSet(event, "actor_ref", GameApi::MakeActorRef(actor));
	DictSet(event, "inflictor_ref", GameApi::MakeActorRef(inflictor));
	InvokeEvent("actor_died", event, actor, ActorPlayerNumber(actor));
}

void OnActorDamaged(AActor* actor, AActor* inflictor, AActor* source,
	int damage, const char* damageType, int flags, double angle)
{
	if (!HasCallbacks("actor_damaged")) return;
	PyObject* event = BuildEvent("actor_damaged");
	DictSet(event, "actor_ref", GameApi::MakeActorRef(actor));
	DictSet(event, "inflictor_ref", GameApi::MakeActorRef(inflictor));
	DictSet(event, "source_ref", GameApi::MakeActorRef(source));
	DictSetInt(event, "damage", damage);
	DictSetString(event, "damage_type", damageType);
	DictSetInt(event, "flags", flags);
	DictSetFloat(event, "angle", angle);
	InvokeEvent("actor_damaged", event, actor, ActorPlayerNumber(actor));
}

void OnActorDestroyed(AActor* actor)
{
	if (!HasCallbacks("actor_destroyed")) return;
	PyObject* event = BuildEvent("actor_destroyed");
	DictSet(event, "actor_ref", GameApi::MakeActorRef(actor));
	DictSet(event, "actor", ActorSnapshot(actor));
	InvokeEvent("actor_destroyed", event, actor, ActorPlayerNumber(actor));
}

void OnActorRevived(AActor* actor)
{
	if (!HasCallbacks("actor_revived")) return;
	PyObject* event = BuildEvent("actor_revived");
	DictSet(event, "actor_ref", GameApi::MakeActorRef(actor));
	InvokeEvent("actor_revived", event, actor, ActorPlayerNumber(actor));
}

void OnLineActivated(int lineIndex, AActor* actor, int activationType)
{
	if (!HasCallbacks("line_activated")) return;
	PyObject* event = BuildEvent("line_activated");
	DictSetInt(event, "line_index", lineIndex);
	DictSet(event, "actor_ref", GameApi::MakeActorRef(actor));
	DictSetInt(event, "activation_type", activationType);
	InvokeEvent("line_activated", event, actor, ActorPlayerNumber(actor));
}

void OnLineActivationFailed(int lineIndex, int special, const int* args, AActor* actor, int activationType)
{
	if (!HasCallbacks("line_activation_failed")) return;
	PyObject* event = BuildEvent("line_activation_failed");
	DictSetInt(event, "line_index", lineIndex);
	DictSetInt(event, "special", special);
	PyObject* argList = PyList_New(5);
	if (argList != nullptr)
	{
		for (int i = 0; i < 5; ++i)
			PyList_SET_ITEM(argList, i, PyLong_FromLong(args == nullptr ? 0 : args[i]));
		DictSet(event, "args", argList);
	}
	DictSet(event, "actor_ref", GameApi::MakeActorRef(actor));
	DictSetInt(event, "activation_type", activationType);
	InvokeEvent("line_activation_failed", event, actor, ActorPlayerNumber(actor));
}

void OnPlayerEvent(const char* eventName, int playerIndex, bool fromHub)
{
	if (!IsKnownEvent(eventName) || !HasCallbacks(eventName)) return;
	PyObject* event = BuildEvent(eventName);
	DictSetInt(event, "player_index", playerIndex);
	DictSetBool(event, "from_hub", fromHub);
	AActor* actor = playerIndex >= 0 && playerIndex < static_cast<int>(MAXPLAYERS)
		? players[playerIndex].mo : nullptr;
	DictSet(event, "actor_ref", GameApi::MakeActorRef(actor));
	InvokeEvent(eventName, event, actor, playerIndex);
}

void SerializeState(FSerializer& arc)
{
	if (!active) return;
	FString encoded;
	if (arc.isWriting())
	{
		const bool wasBlocked = gameplayMutationBlocked;
		gameplayMutationBlocked = true;
		InvokeEvent("save", BuildEvent("save"));
		gameplayMutationBlocked = wasBlocked;
		encoded = DumpStateJson().c_str();
	}
	arc("pythonstate", encoded);
	if (arc.isReading() && encoded.IsNotEmpty())
	{
		loadCallbackPending = LoadStateJson(encoded.GetChars());
	}
}

void FinishLoadState()
{
	if (!active || !loadCallbackPending) return;
	loadCallbackPending = false;
	InvokeEvent("load", BuildEvent("load"));
}

void PrintStatus()
{
	Printf("Python scripting: compiled (CPython %s), runtime %s, trust opt-in %s\n",
		PY_VERSION, active ? "active" : "inactive", RuntimeRequested() ? "enabled" : "disabled");
	Printf("Python manifests: %zu valid script entr%s; modules: %zu; callbacks: %zu\n",
		discoveredScripts.size(), discoveredScripts.size() == 1 ? "y" : "ies", modules.size(), callbacks.size());
	for (const ScriptModule& module : modules)
	{
		Printf("  %s (resource container %d)\n", module.Path.c_str(), module.Container);
	}
	if (active)
	{
		Printf("Python tick budget: %d ms, hard between-callback enforcement %s, overrun disable limit %d, skips: %llu, overruns: %llu\n",
			static_cast<int>(py_tick_budget_ms), py_tick_hard_budget ? "on" : "off",
			static_cast<int>(py_tick_overrun_limit),
			static_cast<unsigned long long>(tickBudgetSkips),
			static_cast<unsigned long long>(tickBudgetOverruns));
		for (const Callback& callback : callbacks)
		{
			if (callback.Calls == 0 && callback.BudgetSkips == 0) continue;
			Printf("  %s %s: %llu calls, avg %.3f ms, max %.3f ms, %llu skips, %llu overruns%s\n",
				callback.Event.c_str(), callback.Source.c_str(),
				static_cast<unsigned long long>(callback.Calls),
				callback.Calls == 0 ? 0.0 : callback.TotalMicroseconds / (1000.0 * callback.Calls),
				callback.MaximumMicroseconds / 1000.0,
				static_cast<unsigned long long>(callback.BudgetSkips),
				static_cast<unsigned long long>(callback.BudgetOverruns),
				callback.BudgetDisabled ? ", disabled" : "");
		}
	}
}

#else // BIASEDDOOM_PYTHON

bool IsCompiled() { return false; }
bool IsActive() { return false; }
bool Initialize()
{
	const bool disabled = Args != nullptr && Args->CheckParm("-nopython");
	if (!disabled && (py_enabled || (Args != nullptr && Args->CheckParm("-python"))))
	{
		Printf(TEXTCOLOR_YELLOW "Python scripting was requested, but this executable was built without CPython support. ACS and ZScript are still available.\n");
	}
	return false;
}
void Shutdown() {}
bool Reload() { return false; }
void OnWorldLoaded() {}
void OnWorldUnloaded(const char*) {}
void OnWorldPreTick() {}
void OnWorldTick() {}
void OnWorldPostTick() {}
void OnActorSpawned(AActor*) {}
void OnActorDied(AActor*, AActor*) {}
void OnActorDamaged(AActor*, AActor*, AActor*, int, const char*, int, double) {}
void OnActorDestroyed(AActor*) {}
void OnActorRevived(AActor*) {}
void OnLineActivated(int, AActor*, int) {}
void OnLineActivationFailed(int, int, const int*, AActor*, int) {}
void OnPlayerEvent(const char*, int, bool) {}
unsigned int GetErrorCount() { return 0; }
void SetErrorLogPath(const char*) {}
void DumpStub(const char*)
{
	Printf("Python scripting is not compiled into this executable.\n");
}
void SerializeState(FSerializer&) {}
void FinishLoadState() {}
bool CheckApiThread() { return false; }
bool CheckGameplayMutation() { return false; }
bool CheckSessionMutation() { return false; }
void PrintStatus()
{
	Printf("Python scripting: not compiled into this executable. Configure with -DBIASEDDOOM_ENABLE_PYTHON=ON and CPython 3.10+ development files.\n");
}

#endif // BIASEDDOOM_PYTHON
} // namespace PythonRuntime

CCMD(py_status)
{
	PythonRuntime::PrintStatus();
}

CCMD(dumppystub)
{
	PythonRuntime::DumpStub(argv.argc() > 1 ? argv[1] : nullptr);
}

UNSAFE_CCMD(py_reload)
{
	if (!PythonRuntime::IsCompiled())
	{
		PythonRuntime::PrintStatus();
		return;
	}
	if (!PythonRuntime::Reload())
	{
		Printf(TEXTCOLOR_RED "Python scripts could not be reloaded. Check the preceding log messages.\n");
	}
}
