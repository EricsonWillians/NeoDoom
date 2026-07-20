#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <zmusic.h>

#ifdef BIASEDDOOM_AUDIO_PROBE_OPENAL
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#endif

namespace
{
bool TestDecoder(const char* path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input)
	{
		std::cerr << "Could not open audio fixture: " << path << '\n';
		return false;
	}

	std::vector<uint8_t> data(std::istreambuf_iterator<char>(input), {});
	if (data.empty())
	{
		std::cerr << "Audio fixture is empty: " << path << '\n';
		return false;
	}

	SoundDecoder* decoder = CreateDecoder(data.data(), data.size(), true);
	if (decoder == nullptr)
	{
		std::cerr << "No decoder accepted: " << path << '\n';
		return false;
	}

	int sampleRate = 0;
	ChannelConfig channels = ChannelConfig_Mono;
	SampleType sampleType = SampleType_UInt8;
	SoundDecoder_GetInfo(decoder, &sampleRate, &channels, &sampleType);

	std::array<uint8_t, 4096> pcm{};
	const size_t decoded = SoundDecoder_Read(decoder, pcm.data(), pcm.size());
	SoundDecoder_Close(decoder);

	if (sampleRate <= 0 || decoded == 0 ||
		(channels != ChannelConfig_Mono && channels != ChannelConfig_Stereo))
	{
		std::cerr << "Decoder returned invalid PCM metadata for: " << path << '\n';
		return false;
	}

	std::cout << "Decoded " << path << ": " << decoded << " bytes at "
		<< sampleRate << " Hz, "
		<< (channels == ChannelConfig_Mono ? "mono" : "stereo") << '\n';
	return true;
}

#ifdef BIASEDDOOM_AUDIO_PROBE_OPENAL
bool TestOpenAL()
{
	ALCdevice* device = alcOpenDevice(nullptr);
	if (device == nullptr)
	{
		std::cerr << "OpenAL could not open its default device. "
			"For headless tests, set ALSOFT_DRIVERS=null.\n";
		return false;
	}

	ALCcontext* context = alcCreateContext(device, nullptr);
	if (context == nullptr || alcMakeContextCurrent(context) == ALC_FALSE)
	{
		std::cerr << "OpenAL could not create a context.\n";
		if (context != nullptr) alcDestroyContext(context);
		alcCloseDevice(device);
		return false;
	}

	ALuint buffer = 0;
	ALuint source = 0;
	const std::array<int16_t, 32> silence{};
	alGenBuffers(1, &buffer);
	alBufferData(buffer, AL_FORMAT_MONO16, silence.data(),
		static_cast<ALsizei>(silence.size() * sizeof(silence[0])), 22050);
	alGenSources(1, &source);
	alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
	alSourcePlay(source);
	ALenum error = alGetError();

	if (alcIsExtensionPresent(device, "ALC_SOFT_reopen_device") == ALC_FALSE)
	{
		std::cerr << "Bundled OpenAL is missing ALC_SOFT_reopen_device.\n";
		error = AL_INVALID_OPERATION;
	}
	else
	{
		auto reopen = reinterpret_cast<LPALCREOPENDEVICESOFT>(
			alcGetProcAddress(device, "alcReopenDeviceSOFT"));
		if (reopen == nullptr || reopen(device, nullptr, nullptr) == ALC_FALSE)
		{
			std::cerr << "OpenAL could not reopen its default device.\n";
			error = AL_INVALID_OPERATION;
		}
		else
		{
			ALCint connected = ALC_FALSE;
			alcGetIntegerv(device, ALC_CONNECTED, 1, &connected);
			if (connected == ALC_FALSE || alcGetError(device) != ALC_NO_ERROR)
			{
				std::cerr << "OpenAL device remained disconnected after reopen.\n";
				error = AL_INVALID_OPERATION;
			}
			else
			{
				ALint attachedBuffer = 0;
				alGetSourcei(source, AL_BUFFER, &attachedBuffer);
				if (alGetError() != AL_NO_ERROR || attachedBuffer != static_cast<ALint>(buffer))
				{
					std::cerr << "OpenAL source state was not preserved across device reopen.\n";
					error = AL_INVALID_OPERATION;
				}
				else
				{
					std::cout << "OpenAL in-place device reopen preserved playback state.\n";
				}
			}
		}
	}

	const char* version = reinterpret_cast<const char*>(alGetString(AL_VERSION));
	const ALCchar* name = alcGetString(device, ALC_DEVICE_SPECIFIER);
	std::cout << "OpenAL device: " << (name != nullptr ? name : "(unknown)")
		<< "; version: " << (version != nullptr ? version : "(unknown)") << '\n';

	alDeleteSources(1, &source);
	alDeleteBuffers(1, &buffer);
	alcMakeContextCurrent(nullptr);
	alcDestroyContext(context);
	alcCloseDevice(device);

	if (error != AL_NO_ERROR)
	{
		std::cerr << "OpenAL buffer/source/playback smoke test failed with error "
			<< error << ".\n";
		return false;
	}
	return true;
}
#endif
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cerr << "Usage: biaseddoom-audio-probe <audio-file> [audio-file ...]\n";
		return 2;
	}

	bool ok = true;
#ifdef BIASEDDOOM_AUDIO_PROBE_OPENAL
	ok = TestOpenAL() && ok;
#else
	std::cout << "OpenAL probe not compiled for this build configuration.\n";
#endif

	for (int i = 1; i < argc; ++i)
	{
		ok = TestDecoder(argv[i]) && ok;
	}

	if (ok) std::cout << "Audio regression probe passed.\n";
	return ok ? 0 : 1;
}
