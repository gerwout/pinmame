/* libpinmame test host: runs a game headless through libpinmame's callback API or, with -p, the plugin
   message API VPX standalone uses, and logs what a host receives.
   host [-p] [-o] [-x] [-P] [-R] [-n GAME2] [-D] [-m mech] [-s switches] GAME FRAMES DIR
     -p  plugin message API (a minimal MsgPluginAPI host); -o sample lamps 1-98 and solenoids 1-64 each frame;
     -x  probe lamps and solenoids 0, -1 and 100000 and pinHeck lamp numbers that do not exist; -m  HandleMechanics mask (default 0);
     -s  file of "frame switch state" lines applied with PinmameSetSwitch; -P  physical outputs (SolMask(2) = 2);
     -R  stop after FRAMES, copy $PINHECK_LINK_LOG to DIR/link1.log, run FRAMES more in a new session (of GAME2 with -n)
     -D  DMD frames raw (PINMAME_DMD_MODE_RAW)
   host -T: messages broadcast from a second thread while this one subscribes (for a ThreadSanitizer build)
   DIR gets api.log, frames.bin (VIDEO and DMD frames: uint32 frame, then the pixels; the plugin's with the top bit
   set) and audio.raw (int16 stereo). */
#include "libpinmame.h"
#include "plugins/ControllerPlugin.h"
#include <atomic>
#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static FILE *logf_, *framef, *audiof;
static std::atomic<int> frame{0}, target{0}, done{0};
static std::mutex logm;
static std::map<int, std::vector<std::pair<int, int>>> switches;
static bool sample_outputs, probe, physout;
static int updates[16], with_data[16], announced[16], counts[16], channels = 2;

static void logl(const char *fmt, ...)
{
	va_list ap;
	std::lock_guard<std::mutex> g(logm);
	va_start(ap, fmt);
	vfprintf(logf_, fmt, ap);
	va_end(ap);
}

/* plugin message API: one endpoint for libpinmame, one for this host; callbacks queued to the main thread */
static std::map<std::string, unsigned> msg_ids;
static std::map<unsigned, std::vector<std::pair<msgpi_msg_callback, void *>>> subs;
static std::mutex msgm;                          /* msg_ids and subs */
static std::vector<std::pair<msgpi_timer_callback, void *>> queue;
static std::mutex qm;
static std::thread::id main_id;
static unsigned MSGPIAPI GetPluginEndpoint(const char *) { return 0; }
static void MSGPIAPI GetEndpointInfo(const uint32_t, MsgEndpointInfo *) {}
static unsigned MSGPIAPI GetMsgID(const char *ns, const char *name)
{
	std::string k = std::string(ns) + "." + name;
	std::lock_guard<std::mutex> g(msgm);
	auto it = msg_ids.find(k);
	if (it != msg_ids.end()) return it->second;
	unsigned id = (unsigned)msg_ids.size() + 1;
	msg_ids[k] = id;
	return id;
}
static void MSGPIAPI SubscribeMsg(const uint32_t, const unsigned id, const msgpi_msg_callback cb, void *ud)
{
	std::lock_guard<std::mutex> g(msgm);
	subs[id].push_back({cb, ud});
}
static void MSGPIAPI UnsubscribeMsg(const unsigned id, const msgpi_msg_callback cb, void *ud)
{
	std::lock_guard<std::mutex> g(msgm);
	auto it = subs.find(id);
	if (it == subs.end()) return;
	auto &v = it->second;
	for (size_t i = 0; i < v.size(); i++)
		if (v[i].first == cb && v[i].second == ud) { v.erase(v.begin() + i); break; }
}
static bool subscribed(const unsigned id)
{
	std::lock_guard<std::mutex> g(msgm);
	auto it = subs.find(id);
	return it != subs.end() && !it->second.empty();
}
static void MSGPIAPI BroadcastMsg(const uint32_t, const unsigned id, void *data)
{
	std::vector<std::pair<msgpi_msg_callback, void *>> v;
	{
		std::lock_guard<std::mutex> g(msgm);
		auto it = subs.find(id);
		if (it == subs.end()) return;
		v = it->second;
	}
	for (auto &s : v) s.first(id, s.second, data);
}
static void MSGPIAPI SendMsg(const uint32_t ep, const unsigned id, const uint32_t, void *data) { BroadcastMsg(ep, id, data); }
static void MSGPIAPI ReleaseMsgID(const unsigned) {}
static void MSGPIAPI RegisterSetting(const uint32_t, MsgSettingDef *) {}
static void MSGPIAPI SaveSetting(const uint32_t, MsgSettingDef *) {}
static void MSGPIAPI RunOnMainThread(const uint32_t, const double, const msgpi_timer_callback cb, void *ud)
{
	if (std::this_thread::get_id() == main_id) { cb(ud); return; }
	std::lock_guard<std::mutex> g(qm);
	queue.push_back({cb, ud});
}
static void run_queue()
{
	std::vector<std::pair<msgpi_timer_callback, void *>> q;
	{
		std::lock_guard<std::mutex> g(qm);
		q.swap(queue);
	}
	for (auto &c : q) c.first(c.second);
}
static void MSGPIAPI FlushPendingCallbacks(const uint32_t) { if (std::this_thread::get_id() == main_id) run_queue(); }
static MsgPluginAPI api = { 1, GetPluginEndpoint, GetEndpointInfo, GetMsgID, SubscribeMsg, UnsubscribeMsg, BroadcastMsg, SendMsg,
	ReleaseMsgID, RegisterSetting, SaveSetting, RunOnMainThread, FlushPendingCallbacks };

static std::vector<DisplaySrcId> displays;
static std::vector<StateSrcId> groups;
static std::atomic<int> plugin_ready{0};
static unsigned last_frame_id = ~0u;

static void MSGPIAPI OnAudio(const unsigned, void *, void *data)
{
	AudioUpdateMsg *m = (AudioUpdateMsg *)data;
	if (!m->buffer) return;
	if (m->sampleFormat != CTLPI_AUDIO_FORMAT_SAMPLE_INT16 || m->channelFormat != CTLPI_AUDIO_FORMAT_CHANNEL_STEREO) {
		logl("plugin audio format %u channels %u\n", m->sampleFormat, m->channelFormat);
		return;
	}
	fwrite(m->buffer, 1, m->bufferSize, audiof);
}

static void plugin_enumerate()
{
	GetDisplaySrcMsg dm = { 0, 0, nullptr };
	SendMsg(1, GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_GET_SRC_MSG), 0, &dm);
	displays.resize(dm.count);
	dm.maxEntryCount = dm.count;
	dm.count = 0;
	dm.entries = displays.data();
	SendMsg(1, GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_GET_SRC_MSG), 0, &dm);
	for (auto &d : displays) logl("plugin display %ux%u format %u hardware %08x\n", d.width, d.height, d.frameFormat, d.hardware);
	GetStateSrcMsg sm = { 0, 0, nullptr };
	SendMsg(1, GetMsgID(CTLPI_NAMESPACE, CTLPI_STATE_GET_SRC_MSG), 0, &sm);
	groups.resize(sm.count);
	sm.maxEntryCount = sm.count;
	sm.count = 0;
	sm.entries = groups.data();
	SendMsg(1, GetMsgID(CTLPI_NAMESPACE, CTLPI_STATE_GET_SRC_MSG), 0, &sm);
	for (auto &g : groups) {
		std::string ids;
		for (unsigned i = 0; i < g.nStates; i++) ids += " " + std::to_string((int)g.stateDefs[i].mappingId);
		logl("plugin group %s %u:%s\n", g.name, g.nStates, ids.c_str());
	}
	plugin_ready = 1;
}

/* one byte per value: floats 0..1 scaled like libpinmame's saturatedByte, integers as they are */
static int state_byte(const StateDef &s)
{
	union { uint8_t u8; int32_t i32; float f; } v;
	memset(&v, 0, sizeof(v));
	s.GetState(s.callContext, &v);
	if (s.dataFormat == CTLPI_STATE_FORMAT_FLOAT) return (int)(255.0f * (v.f < 0.0f ? 0.0f : v.f > 1.0f ? 1.0f : v.f));
	if (s.dataFormat == CTLPI_STATE_FORMAT_INT32) return v.i32;
	return v.u8;
}

static std::map<std::string, int> last;
static void change(std::string &line, const std::string &k, int v)
{
	auto it = last.find(k);
	if (it != last.end() && it->second == v) return;
	last[k] = v;
	line += " " + k + "=" + std::to_string(v);
}

static const int bad_numbers[] = { 9, 10, 19, 20, 29, 30, 50, 89, 90, 99, 100, 109, 119, 120, 128 };

static void sample(int f)
{
	std::string line;
	if (sample_outputs) {
		for (int n = 1; n <= 98; n++) if (n % 10 >= 1 && n % 10 <= 8) change(line, "L" + std::to_string(n), PinmameGetLamp(n));
		for (int n = 1; n <= 64; n++) change(line, "S" + std::to_string(n), PinmameGetSolenoid(n));
		for (int n = 1; n <= 98; n++) if (n % 10 >= 1 && n % 10 <= 8) change(line, "W" + std::to_string(n), PinmameGetSwitch(n) != 0);
	}
	if (probe) {
		for (int n : bad_numbers) change(line, "X" + std::to_string(n), PinmameGetLamp(n));
		for (int n : { 0, -1, 100000 }) {
			change(line, "X" + std::to_string(n), PinmameGetLamp(n));
			change(line, "Y" + std::to_string(n), PinmameGetSolenoid(n));
		}
	}
	if (plugin_ready) {
		for (auto &g : groups) {
			const char *p = !strcmp(g.name, "Solenoids") ? "PS" : !strcmp(g.name, "Lamps") ? "PL" : nullptr;
			if (!p) continue;
			for (unsigned i = 0; i < g.nStates; i++) change(line, p + std::to_string((int)g.stateDefs[i].mappingId), state_byte(g.stateDefs[i]));
		}
		for (auto &d : displays) {
			DisplayFrame fr = d.GetRenderFrame(d.callContext);
			if (fr.frameId == last_frame_id) continue;
			last_frame_id = fr.frameId;
			const size_t n = (size_t)d.width * d.height * (d.frameFormat == CTLPI_DISPLAY_FORMAT_SRGB565 ? 2 : d.frameFormat == CTLPI_DISPLAY_FORMAT_SRGB888 ? 3 : 4);
			uint32_t tag = (uint32_t)f | 0x80000000u;
			fwrite(&tag, 4, 1, framef);
			fwrite(fr.frame, 1, n, framef);
		}
	}
	if (!line.empty()) logl("O %d%s\n", f, line.c_str());
}

static void PINMAMECALLBACK OnStateUpdated(int state, void *const) { logl("state %d\n", state); }
static void PINMAMECALLBACK OnDisplayAvailable(int index, int count, PinmameDisplayLayout *l, void *const)
{
	logl("avail %d %d type %d %dx%d depth %d length %d\n", index, count, l->type, l->width, l->height, l->depth, l->length);
	if (index >= 0 && index < 16) { announced[index]++; counts[index] = count; }
}
static void PINMAMECALLBACK OnDisplayUpdated(int index, void *data, PinmameDisplayLayout *l, void *const)
{
	if (index >= 0 && index < 16) { updates[index]++; if (data) with_data[index]++; }
	if (index != 0) return;
	const int f = ++frame;
	if (data && (l->type & PINMAME_DISPLAY_TYPE_SEGMASK) == PINMAME_DISPLAY_TYPE_VIDEO) {
		uint32_t tag = (uint32_t)f;
		fwrite(&tag, 4, 1, framef);
		fwrite(data, 1, (size_t)l->width * l->height * (l->depth == 16 ? 2 : 3), framef);
	} else if (data && (l->type & PINMAME_DISPLAY_TYPE_SEGMASK) == PINMAME_DISPLAY_TYPE_DMD) {
		uint32_t tag = (uint32_t)f;                  /* one byte per dot: luminance 0-255 */
		fwrite(&tag, 4, 1, framef);
		fwrite(data, 1, (size_t)l->width * l->height, framef);
	}
	auto it = switches.find(f);
	if (it != switches.end()) for (auto &s : it->second) PinmameSetSwitch(s.first, s.second);
	sample(f);
	if (f >= target) done = 1;
}
static int PINMAMECALLBACK OnAudioAvailable(PinmameAudioInfo *a, void *const)
{
	logl("audio format %d channels %d rate %.2f fps %.2f\n", a->format, a->channels, a->sampleRate, a->framesPerSecond);
	channels = a->channels;
	return a->samplesPerFrame;
}
static int PINMAMECALLBACK OnAudioUpdated(void *buf, int samples, void *const)
{
	fwrite(buf, 2 * (size_t)channels, (size_t)samples, audiof);
	return samples;
}
static void PINMAMECALLBACK OnLogMessage(PINMAME_LOG_LEVEL level, const char *fmt, va_list args, void *const)
{
	char b[1024];
	vsnprintf(b, sizeof(b), fmt, args);
	if (level == PINMAME_LOG_LEVEL_ERROR) logl("error %s\n", b);
}
static int PINMAMECALLBACK IsKeyPressed(PINMAME_KEYCODE, void *const) { return 0; }

static void MSGPIAPI OnNothing(const unsigned, void *, void *) {}
static int thread_check()
{
	const unsigned id = GetMsgID("test", "msg");
	std::thread t([id] {
		for (int i = 0; i < 20000; i++) {
			BroadcastMsg(0, id, nullptr);
			BroadcastMsg(0, id + 1 + i % 64, nullptr);
			GetMsgID("test", std::to_string(i % 64).c_str());
		}
	});
	for (int i = 0; i < 20000; i++) {
		SubscribeMsg(0, id, OnNothing, nullptr);
		UnsubscribeMsg(id, OnNothing, nullptr);
	}
	t.join();
	printf("threads: message ids and subscriptions shared with a second thread\n");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "-T")) return thread_check();
	bool plugin = false, restart = false, raw = false;
	const char *game2 = nullptr;
	int mech = 0, a = 1;
	for (; a < argc && argv[a][0] == '-'; a++) {
		if (!strcmp(argv[a], "-p")) plugin = true;
		else if (!strcmp(argv[a], "-o")) sample_outputs = true;
		else if (!strcmp(argv[a], "-x")) probe = true;
		else if (!strcmp(argv[a], "-P")) physout = true;
		else if (!strcmp(argv[a], "-R")) restart = true;
		else if (!strcmp(argv[a], "-D")) raw = true;
		else if (!strcmp(argv[a], "-n") && a + 1 < argc) game2 = argv[++a];
		else if (!strcmp(argv[a], "-m") && a + 1 < argc) mech = atoi(argv[++a]);
		else if (!strcmp(argv[a], "-s") && a + 1 < argc) {
			FILE *f = fopen(argv[++a], "r");
			int fr, sw, st;
			if (!f) return 2;
			while (fscanf(f, "%d %d %d", &fr, &sw, &st) == 3) switches[fr].push_back({sw, st});
			fclose(f);
		} else return 2;
	}
	if (argc - a != 3) {
		fprintf(stderr, "usage: host [-p] [-o] [-x] [-P] [-R] [-n GAME2] [-D] [-m mech] [-s switches] GAME FRAMES DIR\n");
		return 2;
	}
	const char *game = argv[a], *dir = argv[a + 2];
	target = atoi(argv[a + 1]);
	main_id = std::this_thread::get_id();
	std::string d(dir);
	logf_ = fopen((d + "/api.log").c_str(), "w");
	if (logf_) setvbuf(logf_, nullptr, _IOLBF, 0);
	framef = fopen((d + "/frames.bin").c_str(), "wb");
	audiof = fopen((d + "/audio.raw").c_str(), "wb");
	if (!logf_ || !framef || !audiof) return 2;
	PinmameConfig config = { PINMAME_AUDIO_FORMAT_INT16, 44100, "", &OnStateUpdated, &OnDisplayAvailable, &OnDisplayUpdated,
		&OnAudioAvailable, &OnAudioUpdated, nullptr, nullptr, nullptr, nullptr, &IsKeyPressed, &OnLogMessage, nullptr };
	snprintf((char *)config.vpmPath, PINMAME_MAX_PATH, "%s/", dir);
	PinmameSetConfig(&config);
	PinmameSetHandleKeyboard(0);
	PinmameSetHandleMechanics(mech);
	if (physout) PinmameSetSolenoidMask(2, 2);
	if (raw) PinmameSetDmdMode(PINMAME_DMD_MODE_RAW);
	if (plugin) {
		PinmameSetMsgAPI(&api, 0);
		SubscribeMsg(1, GetMsgID(CTLPI_NAMESPACE, CTLPI_AUDIO_ON_UPDATE_MSG), OnAudio, nullptr);
	}
	logl("options plugin %d physout %d mechanics %d\n", plugin, physout, mech);
	const auto t0 = std::chrono::steady_clock::now();
	for (int session = 0; session < (restart ? 2 : 1); session++) {
		if (session) {
			/* what the first session left in the link log, read while the process still runs */
			const char *link = getenv("PINHECK_LINK_LOG");
			FILE *in = link ? fopen(link, "rb") : nullptr, *out = fopen((d + "/link1.log").c_str(), "wb");
			char buf[4096];
			size_t n;
			while (in && out && (n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
			if (in) fclose(in);
			if (out) fclose(out);
			logl("restart after %d frames\n", (int)frame);
			frame = 0;
			done = 0;
			displays.clear();                    /* the first session's sources are gone */
			groups.clear();
			last_frame_id = ~0u;
		}
		if (PinmameRun(session && game2 ? game2 : game) != PINMAME_STATUS_OK) { logl("run failed\n"); return 1; }
		while (!done) {
			run_queue();
			if (plugin && !plugin_ready && !groups.size() && PinmameIsRunning() && subscribed(GetMsgID(CTLPI_NAMESPACE, CTLPI_STATE_GET_SRC_MSG)))
				plugin_enumerate();
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		plugin_ready = 0;
		PinmameStop();
		run_queue();
	}
	for (int i = 0; i < 16; i++)
		if (announced[i] || updates[i]) logl("display %d announced %d count %d updates %d with data %d\n", i, announced[i], counts[i], updates[i], with_data[i]);
	logl("end %d frames %.1f s\n", (int)frame, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	fclose(logf_);
	fclose(framef);
	fclose(audiof);
	return 0;
}
