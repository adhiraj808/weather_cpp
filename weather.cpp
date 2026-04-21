#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#ifdef _WIN32
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif

using namespace std;

namespace
{
    const char *RESET = "\x1b[0m";
    const char *CLEAR = "\x1b[2J\x1b[H";
    const char *HOME = "\x1b[H";
    const char *HIDE = "\x1b[?25l";
    const char *SHOW = "\x1b[?25h";
    const char *BOLD = "\x1b[1m";
    const char *YELLOW = "\x1b[33m";
    const char *BLUE = "\x1b[34m";
    const char *WHITE = "\x1b[37m";
    const char *CYAN = "\x1b[36m";
    const char *MAGENTA = "\x1b[35m";
    const int DEFAULT_REFRESH = 300;
    const int DEFAULT_FPS = 6;
    const int RETRY_DELAY = 20;
    const int LINE_WIDTH = 78;

    atomic<bool> gRunning(true);

    enum class WeatherKind
    {
        Sunny,
        Rainy,
        Snow,
        Thunderstorm,
        Cloudy
    };

    struct Options
    {
        string city;
        string apiKey;
        int refreshSeconds = DEFAULT_REFRESH;
        int fps = DEFAULT_FPS;
        bool audioEnabled = true;
    };

    struct WeatherSnapshot
    {
        string city;
        string country;
        string condition;
        string description;
        double temperatureC = 0.0;
        WeatherKind kind = WeatherKind::Cloudy;
        string updatedAt;
    };

    string repeat(char ch, int count)
    {
        return string(count, ch);
    }

    string trim(const string &value)
    {
        const string whitespace = " \t\r\n";
        const size_t start = value.find_first_not_of(whitespace);
        if (start == string::npos)
            return "";
        const size_t end = value.find_last_not_of(whitespace);
        return value.substr(start, end - start + 1);
    }

    string toLower(string value)
    {
        transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
                  { return static_cast<char>(tolower(ch)); });
        return value;
    }

    string titleCase(string value)
    {
        bool upper = true;
        for (size_t i = 0; i < value.size(); ++i)
        {
            unsigned char ch = static_cast<unsigned char>(value[i]);
            if (isspace(ch) || ch == '-')
            {
                upper = true;
                continue;
            }
            value[i] = upper ? static_cast<char>(toupper(ch)) : static_cast<char>(tolower(ch));
            upper = false;
        }
        return value;
    }

    char pathSep()
    {
#ifdef _WIN32
        return '\\';
#else
        return '/';
#endif
    }

    string joinPath(const string &left, const string &right)
    {
        if (left.empty())
            return right;
        if (left[left.size() - 1] == pathSep())
            return left + right;
        return left + pathSep() + right;
    }

    string parentDirectory(const string &path)
    {
        size_t lastSlash = path.find_last_of("/\\");
        return lastSlash == string::npos ? "." : path.substr(0, lastSlash);
    }

    void appendUnique(vector<string> &values, const string &candidate)
    {
        if (candidate.empty())
            return;
        if (find(values.begin(), values.end(), candidate) == values.end())
            values.push_back(candidate);
    }

    bool fileExists(const string &path)
    {
        FILE *file = fopen(path.c_str(), "rb");
        if (file == NULL)
            return false;
        fclose(file);
        return true;
    }

    string getExecutableDirectory()
    {
#ifdef _WIN32
        char buffer[MAX_PATH];
        DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
        if (length == 0)
            return ".";
        string fullPath(buffer, length);
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(NULL, &size);
        vector<char> buffer(size + 1, '\0');
        if (_NSGetExecutablePath(&buffer[0], &size) != 0)
            return ".";
        string fullPath(&buffer[0]);
#else
        char buffer[4096];
        ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (length <= 0)
            return ".";
        buffer[length] = '\0';
        string fullPath(buffer);
#endif
        size_t lastSlash = fullPath.find_last_of("/\\");
        return lastSlash == string::npos ? "." : fullPath.substr(0, lastSlash);
    }

    string shellQuote(const string &value)
    {
        string quoted = "'";
        for (size_t i = 0; i < value.size(); ++i)
            quoted += value[i] == '\'' ? "'\\''" : string(1, value[i]);
        quoted += "'";
        return quoted;
    }

    bool commandExists(const string &command)
    {
#ifdef _WIN32
        (void)command;
        return false;
#else
        return system(("command -v " + command + " >/dev/null 2>&1").c_str()) == 0;
#endif
    }

    string runCommand(const string &command)
    {
#ifdef _WIN32
        FILE *pipe = _popen(command.c_str(), "r");
#else
        FILE *pipe = popen(command.c_str(), "r");
#endif
        if (pipe == NULL)
            return "";

        string output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != NULL)
            output += buffer;

#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return trim(output);
    }

    string urlEncode(const string &value)
    {
        ostringstream encoded;
        for (size_t i = 0; i < value.size(); ++i)
        {
            unsigned char ch = static_cast<unsigned char>(value[i]);
            if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~')
                encoded << static_cast<char>(ch);
            else if (ch == ' ')
                encoded << "%20";
            else
                encoded << '%' << uppercase << hex << setw(2) << setfill('0')
                        << static_cast<int>(ch) << nouppercase << dec << setfill(' ');
        }
        return encoded.str();
    }

    string extractQuoted(const string &json, const string &field, size_t start)
    {
        size_t key = json.find("\"" + field + "\"", start);
        if (key == string::npos)
            return "";
        size_t colon = json.find(':', key);
        size_t first = json.find('"', colon + 1);
        size_t second = json.find('"', first + 1);
        if (colon == string::npos || first == string::npos || second == string::npos)
            return "";
        return json.substr(first + 1, second - first - 1);
    }

    string extractNumber(const string &json, const string &field, size_t start)
    {
        size_t key = json.find("\"" + field + "\"", start);
        if (key == string::npos)
            return "";
        size_t colon = json.find(':', key);
        size_t pos = colon + 1;
        while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos])))
            ++pos;
        size_t end = pos;
        while (end < json.size())
        {
            char ch = json[end];
            if (!(isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '.'))
                break;
            ++end;
        }
        return json.substr(pos, end - pos);
    }

    string nowString()
    {
        time_t now = time(NULL);
        tm localTime = {};
#ifdef _WIN32
        tm *timeInfo = localtime(&now);
        if (timeInfo != NULL)
            localTime = *timeInfo;
#else
        localtime_r(&now, &localTime);
#endif
        ostringstream formatted;
        formatted << put_time(&localTime, "%Y-%m-%d %H:%M:%S");
        return formatted.str();
    }

    WeatherKind classifyWeather(const string &condition)
    {
        string lowered = toLower(condition);
        if (lowered == "clear")
            return WeatherKind::Sunny;
        if (lowered == "rain" || lowered == "drizzle")
            return WeatherKind::Rainy;
        if (lowered == "snow")
            return WeatherKind::Snow;
        if (lowered == "thunderstorm")
            return WeatherKind::Thunderstorm;
        return WeatherKind::Cloudy;
    }

    const char *accent(WeatherKind kind)
    {
        switch (kind)
        {
        case WeatherKind::Sunny:
            return YELLOW;
        case WeatherKind::Rainy:
            return BLUE;
        case WeatherKind::Snow:
            return WHITE;
        case WeatherKind::Thunderstorm:
            return MAGENTA;
        default:
            return CYAN;
        }
    }

    vector<string> soundNames(WeatherKind kind)
    {
        switch (kind)
        {
        case WeatherKind::Sunny:
            return vector<string>{"sunny day", "sunny", "clear"};
        case WeatherKind::Rainy:
            return vector<string>{"raining", "rainy", "rain"};
        case WeatherKind::Snow:
            return vector<string>{"snowy", "snow"};
        case WeatherKind::Thunderstorm:
            return vector<string>{"thunderstrom", "thunderstorm", "storm"};
        default:
            return vector<string>{"cloudy"};
        }
    }

    vector<string> audioDirectories()
    {
        vector<string> directories;
        const char *envSoundDir = getenv("WEATHER_SOUND_DIR");
        if (envSoundDir != NULL)
            appendUnique(directories, trim(envSoundDir));

#ifdef _WIN32
        appendUnique(directories, "C:\\Users\\madhi\\OneDrive\\Desktop\\PROJECTS\\weather_cpp\\sounds");
#endif

        string executableDirectory = getExecutableDirectory();
        string parentDirectoryPath = parentDirectory(executableDirectory);

        appendUnique(directories, joinPath(executableDirectory, "sounds"));
        appendUnique(directories, joinPath(joinPath(executableDirectory, "assets"), "audio"));
        appendUnique(directories, joinPath(parentDirectoryPath, "sounds"));
        appendUnique(directories, joinPath(joinPath(parentDirectoryPath, "assets"), "audio"));
        appendUnique(directories, joinPath(".", "sounds"));
        appendUnique(directories, joinPath(joinPath(".", "assets"), "audio"));

        return directories;
    }

    string findAudioPath(WeatherKind kind)
    {
        vector<string> directories = audioDirectories();
        vector<string> names = soundNames(kind);
        for (size_t dirIndex = 0; dirIndex < directories.size(); ++dirIndex)
        {
            for (size_t nameIndex = 0; nameIndex < names.size(); ++nameIndex)
            {
                string wavPath = joinPath(directories[dirIndex], names[nameIndex] + ".wav");
                if (fileExists(wavPath))
                    return wavPath;

                string mp3Path = joinPath(directories[dirIndex], names[nameIndex] + ".mp3");
                if (fileExists(mp3Path))
                    return mp3Path;
            }
        }

        return "";
    }

    void playAudioCue(WeatherKind kind, bool enabled)
    {
        if (!enabled)
            return;
        string soundPath = findAudioPath(kind);
        if (soundPath.empty())
            return;
#ifdef _WIN32
        mciSendStringA("close weatherAudio", NULL, 0, NULL);
        if (mciSendStringA(("open \"" + soundPath + "\" alias weatherAudio").c_str(), NULL, 0, NULL) == 0)
            mciSendStringA("play weatherAudio from 0", NULL, 0, NULL);
#elif defined(__APPLE__)
        system(("afplay " + shellQuote(soundPath) + " >/dev/null 2>&1 &").c_str());
#else
        if (soundPath.size() >= 4 && soundPath.substr(soundPath.size() - 4) == ".wav" && commandExists("paplay"))
            system(("paplay " + shellQuote(soundPath) + " >/dev/null 2>&1 &").c_str());
        else if (soundPath.size() >= 4 && soundPath.substr(soundPath.size() - 4) == ".wav" && commandExists("aplay"))
            system(("aplay " + shellQuote(soundPath) + " >/dev/null 2>&1 &").c_str());
        else if (commandExists("ffplay"))
            system(("ffplay -nodisp -autoexit " + shellQuote(soundPath) + " >/dev/null 2>&1 &").c_str());
#endif
    }

    bool enableAnsi()
    {
#ifdef _WIN32
        HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (consoleHandle == INVALID_HANDLE_VALUE || !GetConsoleMode(consoleHandle, &mode))
            return false;
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(consoleHandle, mode))
            return false;
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif
        return true;
    }

    void clearFallback()
    {
#ifdef _WIN32
        system("cls");
#else
        system("clear");
#endif
    }

    void sleepMillis(long milliseconds)
    {
        if (milliseconds <= 0)
            return;
#ifdef _WIN32
        Sleep(static_cast<DWORD>(milliseconds));
#else
        usleep(static_cast<useconds_t>(milliseconds * 1000));
#endif
    }

    struct TerminalSession
    {
        bool ansiEnabled;
        TerminalSession() : ansiEnabled(enableAnsi())
        {
            if (ansiEnabled)
                cout << HIDE << CLEAR << flush;
        }
        ~TerminalSession()
        {
            if (ansiEnabled)
                cout << SHOW << RESET << flush;
        }
        void render(const string &frame)
        {
            if (ansiEnabled)
                cout << HOME << frame << flush;
            else
            {
                clearFallback();
                cout << frame << flush;
            }
        }
    };

    void onSignal(int)
    {
        gRunning = false;
    }

    int parsePositiveInt(const string &value, const string &flag)
    {
        try
        {
            int parsed = stoi(value);
            if (parsed <= 0)
                throw runtime_error("");
            return parsed;
        }
        catch (const exception &)
        {
            throw runtime_error("Expected a positive integer for " + flag);
        }
    }

    void printUsage(const string &program)
    {
        cout << "Usage: " << program << " [options] [city name]\n\n"
             << "Options:\n"
             << "  --city <name>\n"
             << "  --api-key <key>\n"
             << "  --refresh <seconds>\n"
             << "  --fps <value>\n"
             << "  --no-audio\n"
             << "  --help\n\n"
             << "The API key can also be supplied via OPENWEATHER_API_KEY.\n";
    }

    Options parseOptions(int argc, char *argv[])
    {
        Options options;
        vector<string> cityParts;
        for (int i = 1; i < argc; ++i)
        {
            string argument = argv[i];
            if (argument == "--help" || argument == "-h")
            {
                printUsage(argv[0]);
                exit(0);
            }
            if (argument == "--city" || argument == "--api-key" || argument == "--refresh" || argument == "--fps")
            {
                if (i + 1 >= argc)
                    throw runtime_error("Missing value after " + argument);
                string value = argv[++i];
                if (argument == "--city")
                    options.city = value;
                else if (argument == "--api-key")
                    options.apiKey = value;
                else if (argument == "--refresh")
                    options.refreshSeconds = parsePositiveInt(value, argument);
                else
                    options.fps = parsePositiveInt(value, argument);
                continue;
            }
            if (argument == "--no-audio")
            {
                options.audioEnabled = false;
                continue;
            }
            cityParts.push_back(argument);
        }

        if (options.city.empty() && !cityParts.empty())
        {
            for (size_t i = 0; i < cityParts.size(); ++i)
                options.city += (i == 0 ? "" : " ") + cityParts[i];
        }

        if (options.city.empty())
        {
            cout << "Enter city: ";
            getline(cin, options.city);
            options.city = trim(options.city);
        }

        if (options.apiKey.empty())
        {
            const char *envKey = getenv("OPENWEATHER_API_KEY");
            if (envKey != NULL)
                options.apiKey = envKey;
        }

        if (options.city.empty())
            throw runtime_error("City input is required.");
        if (options.apiKey.empty())
            throw runtime_error("Missing API key. Set OPENWEATHER_API_KEY or pass --api-key.");

        options.refreshSeconds = max(15, options.refreshSeconds);
        options.fps = min(max(1, options.fps), 12);
        return options;
    }

    WeatherSnapshot fetchWeather(const Options &options)
    {
        string url = "https://api.openweathermap.org/data/2.5/weather?q=" +
                     urlEncode(options.city) + "&appid=" + options.apiKey + "&units=metric";
        string payload = runCommand("curl -fsSL \"" + url + "\"");
        if (payload.empty())
            throw runtime_error("Unable to fetch weather data.");

        size_t weatherPos = payload.find("\"weather\"");
        size_t mainPos = payload.find("\"main\"");
        size_t sysPos = payload.find("\"sys\"");
        string condition = extractQuoted(payload, "main", weatherPos);
        string description = extractQuoted(payload, "description", weatherPos);
        string city = extractQuoted(payload, "name", 0);
        string country = extractQuoted(payload, "country", sysPos);
        string tempText = extractNumber(payload, "temp", mainPos);

        if (condition.empty() || city.empty() || country.empty() || tempText.empty())
        {
            string message = extractQuoted(payload, "message", 0);
            throw runtime_error(message.empty() ? "Unexpected API response." : "API error: " + message);
        }

        WeatherSnapshot snapshot;
        snapshot.city = city;
        snapshot.country = country;
        snapshot.condition = condition;
        snapshot.description = description.empty() ? condition : description;
        snapshot.temperatureC = stod(tempText);
        snapshot.kind = classifyWeather(condition);
        snapshot.updatedAt = nowString();
        return snapshot;
    }

    vector<string> makeFrame(WeatherKind kind, size_t tick);
    string renderDashboard(const WeatherSnapshot &snapshot, const string &status, int countdown, size_t tick, bool audioEnabled);

    string formatTemp(double temperature)
    {
        ostringstream out;
        out << fixed << setprecision(1) << temperature;
        return out.str();
    }

    vector<string> makeFrame(WeatherKind kind, size_t tick)
    {
        int phase = static_cast<int>(tick % 4);
        if (kind == WeatherKind::Sunny)
        {
            return phase % 2 == 0
                       ? vector<string>{
                             "                          \\   |   /                            ",
                             "                           .-'-.-.                             ",
                             "                        -- (  SUN ) --                         ",
                             "                           `-.-.-'                             ",
                             "                          /   |   \\                            ",
                             "                                                                ",
                             "                     clear sky and warm light                   "}
                       : vector<string>{"                            .   |   .                           ", "                          \\  .-'-.-.  /                         ", "                        --  (  SUN )  --                        ", "                          / `-.-.-' \\                          ", "                            '  |  '                            ", "                                                                ", "                     clear sky and warm light                   "};
        }

        if (kind == WeatherKind::Rainy)
        {
            string drops = phase < 2 ? "   / / / / / / / / /" : "   \\ \\ \\ \\ \\ \\ \\ \\ \\";
            return vector<string>{
                "                           .--.                                 ",
                "                        .-(    ).                               ",
                "                       (___.__)__)                              ",
                drops + "                               ",
                drops + "                               ",
                drops + "                               ",
                "                     steady rain moving through                 "};
        }

        if (kind == WeatherKind::Snow)
        {
            string flakes = phase < 2 ? "   *   *   *   *   *" : "   o   o   o   o   o";
            return vector<string>{
                "                           .--.                                 ",
                "                        .-(    ).                               ",
                "                       (___.__)__)                              ",
                flakes + "                              ",
                "     *   *   *   *   *   *   *                                  ",
                flakes + "                              ",
                "                     soft snowfall in motion                    "};
        }

        if (kind == WeatherKind::Thunderstorm)
        {
            string bolt1 = phase % 2 == 0 ? "                            /                                   "
                                          : "                          /_/                                   ";
            string bolt2 = phase % 2 == 0 ? "                           /_                                   "
                                          : "                           /                                    ";
            return vector<string>{
                "                           .--.                                 ",
                "                        .-(    ).                               ",
                "                       (___.__)__)                              ",
                bolt1,
                bolt2,
                "                          / /                                   ",
                "                    lightning and thunder nearby                "};
        }

        int shift = phase * 2;
        string pad(shift, ' ');
        return vector<string>{
            pad + "                       .--.      .--.                            ",
            pad + "                    .-(    ). .-(    ).                          ",
            pad + "                   (___.__)__)(___.__)__)                        ",
            "                                                                ",
            "                       cloud cover drifting by                  ",
            "                                                                ",
            "                                                                "};
    }

    string renderDashboard(const WeatherSnapshot &snapshot, const string &status, int countdown, size_t tick, bool audioEnabled)
    {
        vector<string> frame = makeFrame(snapshot.kind, tick);
        ostringstream out;
        out << BOLD << "Live Weather CLI" << RESET << '\n';
        out << repeat('=', LINE_WIDTH) << '\n';
        out << left
            << setw(28) << ("City: " + snapshot.city + ", " + snapshot.country)
            << setw(18) << ("Temp: " + formatTemp(snapshot.temperatureC) + " C")
            << "Weather: " << snapshot.condition << '\n';
        out << left
            << setw(32) << ("Description: " + titleCase(snapshot.description))
            << setw(23) << ("Next refresh: " + to_string(countdown) + "s")
            << "Audio: " << (audioEnabled ? "enabled" : "disabled") << '\n';
        out << "Updated at: " << snapshot.updatedAt << '\n';
        out << repeat('-', LINE_WIDTH) << '\n';
        for (size_t i = 0; i < frame.size(); ++i)
            out << accent(snapshot.kind) << frame[i] << RESET << '\n';
        out << repeat('-', LINE_WIDTH) << '\n';
        out << "Status: " << status << '\n';
        out << "Controls: Ctrl+C to exit | Use --city to override the prompt | Use --no-audio for silent mode\n";
        out << '\n'
            << '\n'
            << '\n';
        return out.str();
    }

    /* -------- MAIN -------- */

} // namespace

int main(int argc, char *argv[])
{
    try
    {
        signal(SIGINT, onSignal);
#ifdef SIGTERM
        signal(SIGTERM, onSignal);
#endif

        Options options = parseOptions(argc, argv);
        WeatherSnapshot current = fetchWeather(options);
        string status = "Live data fetched successfully.";
        WeatherKind lastSound = current.kind;
        playAudioCue(current.kind, options.audioEnabled);

        TerminalSession terminal;
        chrono::milliseconds frameDelay(1000 / options.fps);
        chrono::steady_clock::time_point nextRefresh = chrono::steady_clock::now() + chrono::seconds(options.refreshSeconds);
        chrono::steady_clock::time_point nextFrame = chrono::steady_clock::now();
        size_t tick = 0;

        while (gRunning)
        {
            chrono::steady_clock::time_point now = chrono::steady_clock::now();
            if (now >= nextRefresh)
            {
                try
                {
                    WeatherSnapshot refreshed = fetchWeather(options);
                    current = refreshed;
                    status = "Live data refreshed successfully.";
                    nextRefresh = now + chrono::seconds(options.refreshSeconds);
                    if (current.kind != lastSound)
                    {
                        playAudioCue(current.kind, options.audioEnabled);
                        lastSound = current.kind;
                    }
                }
                catch (const exception &error)
                {
                    status = string("Refresh failed: ") + error.what() + " Retrying in " + to_string(RETRY_DELAY) + "s.";
                    nextRefresh = now + chrono::seconds(RETRY_DELAY);
                }
            }

            int countdown = static_cast<int>(chrono::duration_cast<chrono::seconds>(nextRefresh - now).count());
            if (countdown < 0)
                countdown = 0;

            terminal.render(renderDashboard(current, status, countdown, tick, options.audioEnabled));
            ++tick;
            nextFrame += frameDelay;
            chrono::steady_clock::time_point afterRender = chrono::steady_clock::now();
            if (nextFrame > afterRender)
            {
                long waitMs = static_cast<long>(chrono::duration_cast<chrono::milliseconds>(nextFrame - afterRender).count());
                sleepMillis(waitMs);
            }
            else
            {
                nextFrame = afterRender;
            }
        }

        cout << '\n';
        return 0;
    }
    catch (const exception &error)
    {
        cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
