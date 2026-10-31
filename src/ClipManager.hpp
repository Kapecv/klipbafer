#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <thread>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <ctime>

using namespace geode::prelude;
namespace fs = std::filesystem;

class ClipManager {
public:
    static ClipManager& get() {
        static ClipManager inst;
        return inst;
    }

    fs::path clipsDir() {
        auto p = Mod::get()->getSaveDir() / "clips";
        std::error_code ec;
        fs::create_directories(p, ec);
        return p;
    }

    fs::path bufferDir() {
        auto p = Mod::get()->getSaveDir() / "buffer";
        std::error_code ec;
        fs::create_directories(p, ec);
        return p;
    }

    void start() {
        stop();
        std::error_code ec;
        for (auto& e : fs::directory_iterator(bufferDir(), ec)) {
            fs::remove(e.path(), ec);
        }
        m_active = true;
    }

    void stop() {
        closePipe();
        m_active = false;
    }

    void pushFrame() {
        if (!m_active) return;
        if (!m_pipe && !openPipe()) {
            m_active = false;
            return;
        }
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, m_w, m_h, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
        size_t written = fwrite(m_pixels.data(), 1, m_pixels.size(), m_pipe);
        if (written != m_pixels.size()) {
            closePipe();
            m_active = false;
        }
    }

    void save(std::string levelName) {
        if (!m_active) return;
        closePipe();

        std::error_code ec;
        std::vector<fs::path> segs;
        for (auto& e : fs::directory_iterator(bufferDir(), ec)) {
            if (e.path().extension() == ".ts" && fs::file_size(e.path(), ec) > 0) {
                segs.push_back(e.path());
            }
        }
        if (segs.empty()) {
            Notification::create("Brak nagrania do zapisania", NotificationIcon::Error)->show();
            return;
        }
        std::sort(segs.begin(), segs.end(), [&](auto& a, auto& b) {
            std::error_code e1;
            return fs::last_write_time(a, e1) < fs::last_write_time(b, e1);
        });

        size_t want = Mod::get()->getSettingValue<int64_t>("clip-seconds") / 10 + 1;
        if (segs.size() > want) segs.erase(segs.begin(), segs.end() - want);

        char ts[32];
        std::time_t t = std::time(nullptr);
        std::strftime(ts, sizeof ts, "%Y-%m-%d_%H-%M-%S", std::localtime(&t));

        for (auto& c : levelName) {
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        }
        if (levelName.empty()) levelName = "klip";

        auto tmp = Mod::get()->getSaveDir() / fmt::format("tmp_{}", ts);
        fs::create_directories(tmp, ec);
        std::string list;
        for (auto& s : segs) {
            auto dst = tmp / s.filename();
            fs::rename(s, dst, ec);
            list += fmt::format("file '{}'\n", utils::string::pathToString(dst.filename()));
        }
        fs::path listFile = tmp / "list.txt";
        (void)utils::file::writeString(listFile, list);

        fs::path out = clipsDir() / fmt::format("{}_{}.mp4", levelName, ts);
        std::string ff = Mod::get()->getSettingValue<std::string>("ffmpeg-path");
        std::string cmd = fmt::format(
            "\"\"{}\" -y -loglevel error -f concat -safe 0 -i \"{}\" -c copy -movflags +faststart \"{}\"\"",
            ff, utils::string::pathToString(listFile), utils::string::pathToString(out)
        );

        Notification::create("Zapisuję klip...", NotificationIcon::Loading)->show();

        std::thread([cmd, tmp]() {
            _wsystem(utils::string::utf8ToWide(cmd).c_str());
            std::error_code e2;
            fs::remove_all(tmp, e2);
            Loader::get()->queueInMainThread([] {
                Notification::create("Klip zapisany!", NotificationIcon::Success)->show();
            });
        }).detach();
    }

private:
    FILE* m_pipe = nullptr;
    bool m_active = false;
    int m_w = 0, m_h = 0;
    std::vector<uint8_t> m_pixels;

    void closePipe() {
        if (m_pipe) {
            _pclose(m_pipe);
            m_pipe = nullptr;
        }
    }

    bool openPipe() {
        auto fsz = CCDirector::get()->getOpenGLView()->getFrameSize();
        m_w = static_cast<int>(fsz.width) & ~1;
        m_h = static_cast<int>(fsz.height) & ~1;
        m_pixels.resize(static_cast<size_t>(m_w) * m_h * 4);

        std::string ff = Mod::get()->getSettingValue<std::string>("ffmpeg-path");
        std::string scale = Mod::get()->getSettingValue<bool>("scale-720p") ? ",scale=-2:720" : "";
        size_t segCount = Mod::get()->getSettingValue<int64_t>("clip-seconds") / 10 + 2;
        auto pattern = utils::string::pathToString(bufferDir() / "seg%02d.ts");

        std::string cmd = fmt::format(
            "\"\"{}\" -y -loglevel error -f rawvideo -pix_fmt rgba -s {}x{} "
            "-use_wallclock_as_timestamps 1 -i - -vf vflip{} -r 60 "
            "-c:v libx264 -preset ultrafast -crf 26 -pix_fmt yuv420p -g 60 "
            "-f segment -segment_time 10 -segment_wrap {} -segment_format mpegts "
            "-reset_timestamps 1 \"{}\"\"",
            ff, m_w, m_h, scale, segCount, pattern
        );

        m_pipe = _wpopen(utils::string::utf8ToWide(cmd).c_str(), L"wb");
        if (!m_pipe) {
            Notification::create("Nie mogę uruchomić ffmpeg", NotificationIcon::Error)->show();
            return false;
        }
        return true;
    }
};
