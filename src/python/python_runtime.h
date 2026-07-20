#pragma once

class AActor;
class FSerializer;

namespace PythonRuntime
{
	// The build may contain stubs when CPython development files are absent.
	bool IsCompiled();
	bool IsActive();
	bool Initialize();
	void Shutdown();
	bool Reload();

	void OnWorldLoaded();
	void OnWorldUnloaded(const char* nextMap);
	void OnWorldPreTick();
	void OnWorldTick();
	void OnWorldPostTick();
	void OnActorSpawned(AActor* actor);
	void OnActorDied(AActor* actor, AActor* inflictor);
	void OnActorDamaged(AActor* actor, AActor* inflictor, AActor* source,
		int damage, const char* damageType, int flags, double angle);
	void OnActorDestroyed(AActor* actor);
	void OnActorRevived(AActor* actor);
	void OnLineActivated(int lineIndex, AActor* actor, int activationType);
	void OnPlayerEvent(const char* eventName, int playerIndex, bool fromHub = false);

	// Python's shared state dictionary is stored as JSON in savegames. Reading
	// is split from callback dispatch because actors are restored later.
	void SerializeState(FSerializer& arc);
	void FinishLoadState();

	void PrintStatus();

	// Internal contract used by the separately compiled native gameplay API.
	// Every Python entry point checks these before touching engine state.
	bool CheckApiThread();
	bool CheckGameplayMutation();
	bool CheckSessionMutation();
}
