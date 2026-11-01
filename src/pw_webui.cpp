// SPDX-License-Identifier: GPL-3.0-or-later
// MihawkOS's WebUI: routes (src/pw_webui.hpp says what each one does and how writes stay safe).
#include "pw_webui.hpp"
#include "pw_controller.h"
#include "pw_controls.h"
#include "pw_detailed_logs.h"
#include "pw_drive_browse.h"
#include "pw_game_mode.h"
#include "pw_high_refresh.h"
#include "json.hpp"   // external/tinygltf's vendored nlohmann::json, already in the title
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <map>
#include <tuple>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using json = nlohmann::json;

namespace {

constexpr size_t kBodyLimit = 64 * 1024, kLogLimit = 512 * 1024;

PwWebuiResponse reply(unsigned status, const json &value)
{
    PwWebuiResponse response;
    response.status = status;
    response.body = value.dump();
    return response;
}

PwWebuiResponse fail(unsigned status, const std::string &why) { return reply(status, json{{"error", why}}); }

bool read_file(const std::string &path, std::string &out, size_t limit)
{
    out.clear();
    const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return false;
    char buffer[16384];
    for (ssize_t got; out.size() < limit && (got = read(fd, buffer, std::min(sizeof buffer, limit - out.size()))) > 0;)
        out.append(buffer, size_t(got));
    close(fd);
    return true;
}

// The last limit bytes of a file (a log's end is what a report needs), from the start of a line.
bool read_tail(const std::string &path, std::string &out, size_t limit, bool *cut)
{
    struct stat status;
    *cut = false;
    if (lstat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode)) return false;
    const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return false;
    const off_t from = status.st_size > off_t(limit) ? status.st_size - off_t(limit) : 0;
    out.assign(size_t(status.st_size - from), '\0');
    const ssize_t got = pread(fd, out.data(), out.size(), from);
    close(fd);
    if (got < 0) return false;
    out.resize(size_t(got));
    if (from > 0) {
        *cut = true;
        const size_t line = out.find('\n');
        out.erase(0, line == std::string::npos ? 0 : line + 1);
    }
    return true;
}

// What the page makes under /data is open to every process, as the FTP server's copies and the title's own folders are:
// a game's helpers and the system's apps do not all run as the title's user, and a game must be able to write in its
// own folder. Set after creation, whatever the process's umask.
bool make_dir(const std::string &path) { return mkdir(path.c_str(), 0777) == 0 && chmod(path.c_str(), 0777) == 0; }

bool write_file(const std::string &path, const std::string &text)
{
    const std::string temporary = path + ".tmp";
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0666);
    if (fd < 0) return false;
    bool ok = fchmod(fd, 0666) == 0;
    ok = write(fd, text.data(), text.size()) == ssize_t(text.size()) && ok;
    ok = fsync(fd) == 0 && ok;
    ok = close(fd) == 0 && ok;
    if (ok && rename(temporary.c_str(), path.c_str()) == 0) return true;
    unlink(temporary.c_str());
    return false;
}

bool is_dir(const std::string &path)
{
    struct stat status;
    return lstat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

bool exists(const std::string &path)
{
    struct stat status;
    return lstat(path.c_str(), &status) == 0;
}

struct Entry { std::string name; bool dir; long long size; };

std::vector<Entry> list_dir(const std::string &path)
{
    std::vector<Entry> out;
    DIR *directory = opendir(path.c_str());
    if (!directory) return out;
    while (struct dirent *item = readdir(directory)) {
        const std::string name = item->d_name;
        if (name.empty() || name[0] == '.') continue;
        struct stat status;
        if (lstat((path + "/" + name).c_str(), &status) != 0 || S_ISLNK(status.st_mode)) continue;
        if (S_ISDIR(status.st_mode) || S_ISREG(status.st_mode))
            out.push_back({name, S_ISDIR(status.st_mode), (long long)status.st_size});
    }
    closedir(directory);
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.name < b.name; });
    return out;
}

bool remove_tree(const std::string &path, int depth = 0)
{
    if (depth > 8) return false;
    for (const Entry &entry : list_dir(path)) {
        const std::string child = path + "/" + entry.name;
        if (entry.dir ? !remove_tree(child, depth + 1) : unlink(child.c_str()) != 0) return false;
    }
    return rmdir(path.c_str()) == 0;
}

bool printable(const std::string &text, size_t limit)
{
    if (text.size() > limit) return false;
    for (unsigned char c : text)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

bool token_ok(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    return !context.token.empty() && request.token == context.token;
}

std::string arg(const PwWebuiRequest &request, const char *key)
{
    const auto found = request.args.find(key);
    return found == request.args.end() ? std::string() : found->second;
}

bool parse_body(const PwWebuiRequest &request, json &out)
{
    if (request.body.size() > kBodyLimit) return false;
    out = json::parse(request.body, nullptr, false);
    return out.is_object();
}

// The entry's header art's time (0: none): the page asks for it with ?v= so a new picture is not the old cached one.
long long cover_mtime(const PwWebuiContext &context, const std::string &id)
{
    struct stat status;
    const std::string path = context.data_root + "/covers/" + id + "-header.img";
    return lstat(path.c_str(), &status) == 0 && S_ISREG(status.st_mode) ? (long long)status.st_mtime : 0;
}

json meta_json(const PwWebuiContext &context, const std::string &id)
{
    std::string text;
    if (!read_file(context.data_root + "/games/" + id + "/meta.json", text, 64 * 1024)) return nullptr;
    const json parsed = json::parse(text, nullptr, false);
    return parsed.is_object() ? parsed : json(nullptr);
}

json game_json(const PwWebuiContext &context, const PwWebuiGame &game)
{
    return json{{"id", game.id}, {"name", game.name}, {"detail", game.detail}, {"executable", game.executable},
                {"arguments", game.arguments}, {"directory", game.directory}, {"prefix", game.prefix},
                {"env", game.env}, {"desktop", game.desktop}, {"launcher", game.launcher}, {"x86_32", game.x86_32}, {"complaint", game.complaint},
                {"entry", exists(context.data_root + "/games/" + game.id + "/game.json")},
                {"cover", cover_mtime(context, game.id)}, {"meta", meta_json(context, game.id)}};
}

PwWebuiResponse library(const PwWebuiContext &context)
{
    json games = json::array();
    if (context.games)
        for (const PwWebuiGame &game : context.games()) games.push_back(game_json(context, game));
    return reply(200, json{{"games", games}});
}

// A Windows path the launcher accepts: C:\... or Z:\..., printable, no "..".
bool windows_path(const std::string &path)
{
    return path.size() >= 3 && path.size() <= 260 && printable(path, 260) &&
           (path[0] == 'C' || path[0] == 'c' || path[0] == 'Z' || path[0] == 'z') && path[1] == ':' && path[2] == '\\' &&
           path.find("\\..\\") == std::string::npos && path.compare(path.size() - 3, 3, "\\..") != 0;
}

bool env_word(const std::string &word)
{
    const size_t equals = word.find('=');
    if (equals == 0 || equals == std::string::npos || equals > 64 || !printable(word, 512)) return false;
    for (size_t i = 0; i < equals; ++i) {
        const char c = word[i];
        if (!(std::isupper((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_')) return false;
    }
    return true;
}

PwWebuiResponse save_game(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    json body;
    if (!parse_body(request, body)) return fail(400, "The entry is not a JSON object.");
    const std::string id = body.value("id", std::string());
    if (!pw_webui_id(id)) return fail(400, "The id takes 1 to 64 letters, digits, - and _.");
    auto text = [&body](const char *key, std::string &into, size_t limit) {
        const auto found = body.find(key);
        if (found == body.end()) return true;
        if (!found->is_string() || !printable(found->get<std::string>(), limit)) return false;
        into = found->get<std::string>();
        return true;
    };
    std::string name, detail, executable, arguments, directory, prefix;
    if (!text("name", name, 128) || !text("detail", detail, 128) || !text("executable", executable, 260) ||
        !text("arguments", arguments, 1024) || !text("directory", directory, 260) || !text("prefix", prefix, 64))
        return fail(400, "A field is too long or holds a control character.");
    if (name.empty()) return fail(400, "The entry needs a name.");
    if (!windows_path(executable)) return fail(400, "The program must be a Windows path such as C:\\Games\\Name\\Game.exe.");
    if (!directory.empty() && !windows_path(directory)) return fail(400, "Start in must be a Windows path, or empty.");
    if (!prefix.empty() && !pw_webui_id(prefix)) return fail(400, "The prefix takes 1 to 64 letters, digits, - and _.");
    std::vector<std::string> env;
    if (body.count("env")) {
        if (!body["env"].is_array() || body["env"].size() > 32) return fail(400, "env is a list of up to 32 words.");
        for (const json &word : body["env"])
            if (!word.is_string() || !env_word(word.get<std::string>()))
                return fail(400, "Each env word is NAME=VALUE, the name in capitals, digits and _.");
            else
                env.push_back(word.get<std::string>());
    }
    // Keys this page does not edit stay as they were
    const std::string folder = context.data_root + "/games/" + id, path = folder + "/game.json";
    json entry = json::object();
    std::string old;
    if (read_file(path, old, kBodyLimit)) {
        json parsed = json::parse(old, nullptr, false);
        if (parsed.is_object()) entry = parsed;
    }
    entry["name"] = name;
    entry["executable"] = executable;
    auto set = [&entry](const char *key, const std::string &value) {
        if (value.empty()) entry.erase(key);
        else entry[key] = value;
    };
    set("detail", detail);
    set("arguments", arguments);
    set("directory", directory);
    set("prefix", prefix);
    if (env.empty()) entry.erase("env");
    else entry["env"] = env;
    if (body.count("desktop") && body["desktop"].is_boolean()) {
        if (body["desktop"].get<bool>()) entry.erase("desktop");
        else entry["desktop"] = false;
    }
    if (body.count("launcher") && body["launcher"].is_boolean()) {   // ps5/src/proton_launch.h ProtonGame::launcher
        if (body["launcher"].get<bool>()) entry.erase("launcher");
        else entry["launcher"] = false;
    }
    if (!is_dir(context.data_root + "/games") && !make_dir(context.data_root + "/games"))
        return fail(500, "The games folder cannot be made.");
    if (!is_dir(folder) && !make_dir(folder)) return fail(500, "The entry's folder cannot be made.");
    if (!write_file(path, entry.dump(2) + "\n")) return fail(507, "The entry could not be saved.");
    if (context.changed) context.changed("library");
    return reply(200, json{{"saved", id}});
}

PwWebuiResponse delete_game(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    json body;
    if (!parse_body(request, body)) return fail(400, "The request is not a JSON object.");
    const std::string id = body.value("id", std::string());
    if (!pw_webui_id(id)) return fail(400, "No such entry.");
    const std::string folder = context.data_root + "/games/" + id;
    if (!is_dir(folder)) return fail(404, "No such entry.");
    if (!remove_tree(folder)) return fail(500, "The entry could not be removed.");
    if (context.changed) context.changed("library");
    return reply(200, json{{"removed", id}});
}

// A relative path under a prefix's drive_c, every part validated and none a symlink (src/pw_drive_browse.h, which the
// launcher's Files page uses too).
bool drive_path(const PwWebuiContext &context, const std::string &prefix, const std::string &relative, std::string &out)
{
    char folder[4096];
    if (pw_drive_folder(context.data_root.c_str(), prefix.c_str(), relative.c_str(), folder, sizeof(folder))) return false;
    out = folder;
    return true;
}

PwWebuiResponse browse(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    std::string prefix = arg(request, "prefix"), relative = arg(request, "path"), folder;
    if (prefix.empty()) prefix = "mihawk";
    if (relative.empty()) relative = "Games";
    if (!drive_path(context, prefix, relative, folder)) return fail(404, "No such folder.");
    json dirs = json::array(), exes = json::array();
    PwDriveEntry *list = nullptr;
    size_t count = 0, others = 0;
    if (pw_drive_list(folder.c_str(), &list, &count, &others)) return fail(404, "No such folder.");
    for (size_t i = 0; i < count; ++i)
        if (list[i].dir) dirs.push_back(list[i].name);
        else exes.push_back(json{{"name", list[i].name}, {"size", list[i].size}});
    free(list);
    std::string windows = "C:\\" + relative;
    std::replace(windows.begin(), windows.end(), '/', '\\');
    return reply(200, json{{"prefix", prefix}, {"path", relative}, {"windows", windows}, {"dirs", dirs}, {"exes", exes}});
}

// A theme or music choice (PwWebuiContext::themes, music): the id kept in the title folder's hui/<file>, if the list has
// it; the list's first otherwise ("off" for the music too).
std::string kept_choice(const PwWebuiContext &context, const char *file, const std::vector<PwWebuiChoice> &list, bool off)
{
    std::string id;
    read_file(context.title_root + "/hui/" + file, id, 64);
    while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' ')) id.pop_back();
    if (off && id == "off") return id;
    for (const PwWebuiChoice &choice : list)
        if (choice.id == id) return id;
    return list.empty() ? std::string() : list[0].id;
}

json choices_json(const std::vector<PwWebuiChoice> &list, bool off)
{
    json out = json::array();
    for (const PwWebuiChoice &choice : list) out.push_back(json{{"id", choice.id}, {"label", choice.name}});
    if (off && !list.empty()) out.push_back(json{{"id", "off"}, {"label", "Off"}});
    return out;
}

json settings_json(const PwWebuiContext &context)
{
    const std::string &root = context.data_root;
    int game_mode = 1, controller = PW_CONTROLLER_XBOX_360, logging = 0, high_refresh = 1;
    pw_game_mode_load(&game_mode, (root + "/game-mode.txt").c_str());
    pw_high_refresh_load(&high_refresh, (root + "/high-refresh.txt").c_str());
    pw_controller_load(&controller, (root + "/controller.txt").c_str());
    pw_detailed_logs_load(&logging, (root + "/logging.txt").c_str());
    pw_controls controls;
    pw_controls_load(&controls, (root + "/controls.txt").c_str());
    json buttons = json::array(), controllers = json::array(), keyboards = json::array();
    for (unsigned i = 0; i < PW_OSK_TRIGGERS; ++i)
        keyboards.push_back(json{{"id", pw_osk_trigger_id(i)}});
    for (unsigned i = 0; i < PW_CONTROLS_BUTTONS; ++i)
        buttons.push_back(json{{"id", pw_controls_button_id(i)}, {"label", pw_controls_button_label(i)}});
    for (int k = 0; k < PW_CONTROLLER_KINDS; ++k)
        controllers.push_back(json{{"id", pw_controller_word(k)}, {"label", pw_controller_name(k)}});
    return json{{"game_mode", game_mode != 0},
                {"high_refresh", high_refresh != 0},
                {"controller", pw_controller_word(controller)},
                {"logging", logging != 0},
                {"show_all", exists(root + "/library-show-all")},
                {"mouse", json{{"left", pw_controls_button_id(controls.button[0])},
                               {"right", pw_controls_button_id(controls.button[1])},
                               {"middle", pw_controls_button_id(controls.button[2])}}},
                {"keyboard", pw_osk_trigger_id(controls.keyboard)},
                {"theme", kept_choice(context, "theme.txt", context.themes, false)},
                {"music", kept_choice(context, "music.txt", context.music, true)},
                {"choices", json{{"buttons", buttons}, {"controllers", controllers}, {"keyboards", keyboards},
                                 {"themes", choices_json(context.themes, false)}, {"music", choices_json(context.music, true)}}}};
}

int button_index(const std::string &id)
{
    for (unsigned i = 0; i < PW_CONTROLS_BUTTONS; ++i)
        if (id == pw_controls_button_id(i)) return int(i);
    return -1;
}

// Every key given is checked first, then each is saved: a bad value saves nothing.
PwWebuiResponse save_settings(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    json body;
    if (!parse_body(request, body)) return fail(400, "The settings are not a JSON object.");
    const std::string &root = context.data_root;
    int controller = -1;
    pw_controls controls;
    pw_controls_load(&controls, (root + "/controls.txt").c_str());
    bool controls_changed = false;
    for (const char *key : {"game_mode", "high_refresh", "logging", "show_all"})
        if (body.count(key) && !body[key].is_boolean()) return fail(400, std::string(key) + " is true or false.");
    if (body.count("controller")) {
        controller = body["controller"].is_string() ? pw_controller_kind(body["controller"].get<std::string>().c_str()) : -1;
        if (controller < 0) return fail(400, "The controller is xbox-360, xbox-one or dualsense.");
    }
    if (body.count("mouse")) {
        const json &mouse = body["mouse"];
        if (!mouse.is_object()) return fail(400, "mouse holds left, right and middle.");
        const char *actions[] = {"left", "right", "middle"};
        for (unsigned i = 0; i < 3; ++i) {
            if (!mouse.count(actions[i])) continue;
            const int index = mouse[actions[i]].is_string() ? button_index(mouse[actions[i]].get<std::string>()) : -1;
            if (index < 0) return fail(400, std::string("No such button for the ") + actions[i] + " click.");
            pw_controls_assign(&controls, i, unsigned(index));   // a button keeps one action: the old one is cleared
            controls_changed = true;
        }
    }
    if (body.count("keyboard")) {
        const int trigger = body["keyboard"].is_string() ? pw_osk_trigger_parse(body["keyboard"].get<std::string>().c_str()) : -1;
        if (trigger < 0) return fail(400, "The on-screen keyboard is off, touchpad, l3-r3 or options-touchpad.");
        pw_controls_set_keyboard(&controls, unsigned(trigger));
        controls_changed = true;
    }
    // The theme and the music: an id the launcher lists (and "off" for the music)
    std::vector<std::pair<std::string, std::string>> kept;   // file, id
    for (const auto &[key, file, list, off] : {std::make_tuple("theme", "theme.txt", &context.themes, false),
                                               std::make_tuple("music", "music.txt", &context.music, true)}) {
        if (!body.count(key)) continue;
        const std::string id = body[key].is_string() ? body[key].get<std::string>() : std::string();
        bool known = off && !list->empty() && id == "off";
        for (const PwWebuiChoice &choice : *list) known = known || choice.id == id;
        if (!known) return fail(400, std::string("No such ") + key + ".");
        kept.emplace_back(file, id);
    }
    int error = 0;
    for (const auto &[file, id] : kept) {
        const std::string folder = context.title_root + "/hui";
        if (!is_dir(folder) && !make_dir(folder)) error = error ? error : errno;
        else if (!error && !write_file(folder + "/" + file, id + "\n")) error = EIO;
    }
    if (body.count("game_mode")) error = error ? error : pw_game_mode_save(body["game_mode"].get<bool>(), (root + "/game-mode.txt").c_str());
    if (body.count("high_refresh"))
        error = error ? error : pw_high_refresh_save(body["high_refresh"].get<bool>(), (root + "/high-refresh.txt").c_str());
    if (body.count("logging")) error = error ? error : pw_detailed_logs_save(body["logging"].get<bool>(), (root + "/logging.txt").c_str());
    if (controller >= 0) error = error ? error : pw_controller_save(controller, (root + "/controller.txt").c_str());
    if (controls_changed) error = error ? error : pw_controls_save(&controls, (root + "/controls.txt").c_str());
    if (!error && body.count("show_all")) {
        const std::string marker = root + "/library-show-all";
        if (body["show_all"].get<bool>()) {
            if (!exists(marker) && !write_file(marker, "")) error = EIO;
        } else if (exists(marker) && unlink(marker.c_str()) != 0) {
            error = errno;
        }
    }
    if (error) return fail(507, std::string("A setting could not be saved: ") + std::strerror(error));
    if (context.changed) context.changed("settings");
    return reply(200, settings_json(context));
}

// The logs folder's files and the last sessions' folders, one level down.
// The logs: logs/ itself and its sessions/<n>/ (src/pw_log_sessions.h), each as a name under logs/
std::vector<Entry> log_files(const PwWebuiContext &context)
{
    // logs/ itself first: a report's 16 MiB then always holds this session's logs and their ends (src/pw_capped_log.h).
    std::vector<Entry> files, sessions;
    const std::string root = context.data_root + "/logs";
    for (const Entry &entry : list_dir(root)) {
        if (!entry.dir) {
            files.push_back(entry);
            continue;
        }
        if (entry.name != "sessions") continue;
        for (const Entry &session : list_dir(root + "/sessions"))
            if (session.dir && pw_webui_part(session.name))
                for (Entry file : list_dir(root + "/sessions/" + session.name))
                    if (!file.dir) file.name = "sessions/" + session.name + "/" + file.name, sessions.push_back(file);
    }
    files.insert(files.end(), sessions.begin(), sessions.end());
    return files;
}

PwWebuiResponse logs(const PwWebuiContext &context)
{
    json files = json::array();
    for (const Entry &file : log_files(context)) files.push_back(json{{"name", file.name}, {"size", file.size}});
    return reply(200, json{{"logs", files}});
}

PwWebuiResponse log_text(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    const std::string name = arg(request, "name");
    std::string path = context.data_root + "/logs";
    size_t start = 0, parts = 0;
    while (start <= name.size()) {
        const size_t slash = name.find('/', start);
        const std::string part = name.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!pw_webui_part(part) || ++parts > 3) return fail(404, "No such log.");
        path += "/" + part;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    std::string text;
    bool cut = false;
    if (!read_tail(path, text, kLogLimit, &cut)) return fail(404, "No such log.");
    PwWebuiResponse response;
    response.type = "text/plain; charset=utf-8";
    response.body = (cut ? std::string("[... the start is left out: the last 512 KiB follow ...]\n") : std::string()) +
                    pw_webui_mask(text);
    return response;
}

constexpr size_t kImageLimit = 4u << 20;

std::string url_encode(const std::string &text)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text)
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
        else out += '%', out += hex[c >> 4], out += hex[c & 15];
    return out;
}

// The store's text is HTML: its tags dropped, its common entities decoded (the page shows it as text).
std::string html_text(const std::string &html)
{
    static const struct { const char *entity, *text; } entities[] = {
        {"&quot;", "\""}, {"&amp;", "&"}, {"&#39;", "'"}, {"&apos;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}};
    std::string out;
    for (size_t i = 0; i < html.size();) {
        if (html[i] == '<') {
            const size_t end = html.find('>', i);
            if (end == std::string::npos) break;
            i = end + 1;
            continue;
        }
        bool replaced = false;
        if (html[i] == '&')
            for (const auto &e : entities)
                if (html.compare(i, std::strlen(e.entity), e.entity) == 0) {
                    out += e.text;
                    i += std::strlen(e.entity);
                    replaced = true;
                    break;
                }
        if (!replaced) out += html[i++];
    }
    return out;
}

bool app_id(const std::string &text)
{
    if (text.empty() || text.size() > 10) return false;
    for (unsigned char c : text)
        if (!std::isdigit(c)) return false;
    return true;
}

bool image_bytes(const std::string &data)
{
    return data.size() > 8 && data.size() <= kImageLimit &&
           ((uint8_t(data[0]) == 0xff && uint8_t(data[1]) == 0xd8 && uint8_t(data[2]) == 0xff) ||
            data.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0);
}

std::string image_type(const std::string &data)
{
    return data.compare(0, 4, "\x89PNG", 4) == 0 ? "image/png" : "image/jpeg";
}

std::string cover_path(const PwWebuiContext &context, const std::string &id, const std::string &kind)
{
    return context.data_root + "/covers/" + id + "-" + kind + ".img";
}

bool ensure_dir(const std::string &path)
{
    return is_dir(path) || make_dir(path);
}

bool fetch(const PwWebuiContext &context, const std::string &url, size_t limit, std::string &body, std::string &why)
{
    if (!context.fetch) return why = "This build cannot reach the internet.", false;
    return context.fetch(url, limit, body, why);
}

PwWebuiResponse media_search(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    json body;
    if (!parse_body(request, body)) return fail(400, "The search is not a JSON object.");
    const std::string name = body.value("name", std::string());
    if (name.empty() || !printable(name, 128)) return fail(400, "Search for a name of 1 to 128 characters.");
    std::string text, why;
    if (!fetch(context, "https://store.steampowered.com/api/storesearch/?term=" + url_encode(name) + "&l=english&cc=US",
               256 * 1024, text, why))
        return fail(502, "The store did not answer: " + why);
    const json found = json::parse(text, nullptr, false);
    json matches = json::array();
    if (found.is_object() && found.count("items") && found["items"].is_array())
        for (const json &item : found["items"]) {
            if (!item.is_object() || !item.count("id") || !item["id"].is_number_integer() || !item.count("name")) continue;
            matches.push_back(json{{"appid", std::to_string(item["id"].get<long long>())}, {"name", item.value("name", "")}});
            if (matches.size() == 8) break;
        }
    return reply(200, json{{"matches", matches}});
}

PwWebuiResponse media_thumb(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    const std::string id = arg(request, "appid");
    if (!app_id(id)) return fail(404, "No such app.");
    const std::string folder = context.data_root + "/caches", cache = folder + "/webui", path = cache + "/" + id + ".img";
    std::string data, why;
    if (!read_file(path, data, kImageLimit) || !image_bytes(data)) {
        if (!fetch(context, "https://cdn.akamai.steamstatic.com/steam/apps/" + id + "/capsule_231x87.jpg", kImageLimit, data, why) ||
            !image_bytes(data))
            return fail(404, "No picture for this app.");
        if (ensure_dir(folder) && ensure_dir(cache)) write_file(path, data);
    }
    PwWebuiResponse response;
    response.type = image_type(data);
    response.body = data;
    return response;
}

PwWebuiResponse media_apply(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    json body;
    if (!parse_body(request, body)) return fail(400, "The request is not a JSON object.");
    const std::string id = body.value("id", std::string()), app = body.value("appid", std::string());
    if (!pw_webui_id(id) || !is_dir(context.data_root + "/games/" + id)) return fail(404, "Save the entry first.");
    if (!app_id(app)) return fail(400, "No such app.");
    std::string header, portrait, details, why;
    const std::string cdn = "https://cdn.akamai.steamstatic.com/steam/apps/" + app;
    if (!fetch(context, cdn + "/header.jpg", kImageLimit, header, why) || !image_bytes(header))
        return fail(502, "The store gave no art for this game" + (why.empty() ? std::string(".") : ": " + why));
    if (!fetch(context, cdn + "/library_600x900.jpg", kImageLimit, portrait, why) || !image_bytes(portrait)) portrait.clear();
    json meta{{"appid", app}, {"source", "Steam store"}};
    if (fetch(context, "https://store.steampowered.com/api/appdetails?appids=" + app + "&l=english", 2u << 20, details, why)) {
        const json parsed = json::parse(details, nullptr, false);
        if (parsed.is_object() && parsed.count(app) && parsed[app].is_object() && parsed[app].value("success", false) &&
            parsed[app].count("data") && parsed[app]["data"].is_object()) {
            const json &data = parsed[app]["data"];
            meta["name"] = data.value("name", "");
            meta["description"] = html_text(data.value("short_description", ""));
            for (const char *key : {"developers", "publishers"})
                if (data.count(key) && data[key].is_array()) meta[key] = data[key];
            if (data.count("release_date") && data["release_date"].is_object())
                meta["release"] = data["release_date"].value("date", "");
            json genres = json::array();
            if (data.count("genres") && data["genres"].is_array())
                for (const json &genre : data["genres"])
                    if (genre.is_object()) genres.push_back(genre.value("description", ""));
            meta["genres"] = genres;
        }
    }
    if (!ensure_dir(context.data_root + "/covers") || !write_file(cover_path(context, id, "header"), header) ||
        (!portrait.empty() && !write_file(cover_path(context, id, "portrait"), portrait)) ||
        !write_file(context.data_root + "/games/" + id + "/meta.json", meta.dump(2) + "\n"))
        return fail(507, "The art could not be saved.");
    if (context.changed) context.changed("library");
    return reply(200, json{{"meta", meta}, {"portrait", !portrait.empty()}});
}

PwWebuiResponse media_cover(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    const std::string id = arg(request, "id"), kind = arg(request, "kind").empty() ? "header" : arg(request, "kind");
    if (!pw_webui_id(id) || (kind != "header" && kind != "portrait")) return fail(404, "No such picture.");
    const std::string path = cover_path(context, id, kind);
    if (request.method == "PUT") {
        if (!is_dir(context.data_root + "/games/" + id)) return fail(404, "Save the entry first.");
        if (!image_bytes(request.body)) return fail(400, "Send a JPEG or PNG picture of up to 4 MiB.");
        if (!ensure_dir(context.data_root + "/covers") || !write_file(path, request.body))
            return fail(507, "The picture could not be saved.");
        if (context.changed) context.changed("library");
        return reply(200, json{{"saved", kind}});
    }
    std::string data;
    if (!read_file(path, data, kImageLimit) || !image_bytes(data)) return fail(404, "No such picture.");
    PwWebuiResponse response;
    response.type = image_type(data);
    response.body = data;
    return response;
}

PwWebuiResponse media_remove(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    json body;
    if (!parse_body(request, body)) return fail(400, "The request is not a JSON object.");
    const std::string id = body.value("id", std::string());
    if (!pw_webui_id(id)) return fail(404, "No such entry.");
    for (const char *kind : {"header", "portrait"}) unlink(cover_path(context, id, kind).c_str());
    unlink((context.data_root + "/games/" + id + "/meta.json").c_str());
    if (context.changed) context.changed("library");
    return reply(200, json{{"removed", id}});
}

// ---- forwarders: home screen tiles (forwarder/), PS5_RetroArch's design on MihawkOS's own range ----
constexpr unsigned kFirstTile = 99300, kLastTile = 99399;
// The reasons below are details the page shows under its own words, which name the tile app: the title's binary never
// holds that name, which tools/publication-check.py reads as a copy of the app itself (MihawkOS ships none).
const char *const kNoShadowmount = "Nothing answers on 127.0.0.1:10101.";

std::string tile_id(unsigned number) { return "PPSA" + std::to_string(number); }

bool is_tile_id(const std::string &id)
{
    if (id.size() != 9 || id.compare(0, 4, "PPSA") != 0 || !app_id(id.substr(4))) return false;
    const unsigned number = unsigned(std::stoul(id.substr(4)));
    return number >= kFirstTile && number <= kLastTile;
}

std::string tile_folder(const PwWebuiContext &context, const std::string &id) { return context.homebrew_root + "/" + id; }

bool is_tile(const PwWebuiContext &context, const std::string &id)
{
    return is_tile_id(id) && exists(tile_folder(context, id) + "/forward.txt");
}

std::string base64_decode(const std::string &text)
{
    std::string out;
    unsigned value = 0;
    int bits = -8;
    for (unsigned char c : text) {
        int digit;
        if (c >= 'A' && c <= 'Z') digit = c - 'A';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 26;
        else if (c >= '0' && c <= '9') digit = c - '0' + 52;
        else if (c == '+') digit = 62;
        else if (c == '/') digit = 63;
        else if (c == '=') break;
        else continue;
        value = (value << 6) | unsigned(digit);
        bits += 6;
        if (bits >= 0) {
            out += char((value >> bits) & 0xff);
            bits -= 8;
        }
    }
    return out;
}

bool copy_file(const std::string &from, const std::string &to)
{
    std::string data;
    return read_file(from, data, 64u << 20) && !data.empty() && write_file(to, data);
}

// A tile's modes, as the system wants an app's: its programs executable (0755: a tile whose eboot.bin was 0644 did not
// start, "Can't start the game or app", [Syscore App] error=0x80aa001a errno=13), its folders 0755 and its other files
// 0644. The first tile, 0755 and 0644, showed its picture on the home screen; every tile made 0777 and 0666 since
// showed none, whatever its id, content id or PNG (the base PS5, 2026-10-26).
const char *const kTilePrograms[] = {"/eboot.bin", "/launch.elf", "/sce_module/libc.prx"};
const char *const kTileFolders[] = {"", "/sce_sys", "/sce_module"};
const char *const kTileFiles[] = {"/sce_sys/icon0.png", "/sce_sys/param.json", "/forward.txt"};
bool tile_modes(const std::string &folder)
{
    bool ok = true;
    for (const char *program : kTilePrograms) ok = chmod((folder + program).c_str(), 0755) == 0 && ok;
    for (const char *dir : kTileFolders) ok = chmod((folder + dir).c_str(), 0755) == 0 && ok;
    for (const char *file : kTileFiles) ok = chmod((folder + file).c_str(), 0644) == 0 && ok;
    return ok;
}
bool tile_modes_right(const std::string &folder)
{
    struct stat status{};
    for (const char *program : kTilePrograms)
        if (stat((folder + program).c_str(), &status) != 0 || (status.st_mode & 0777) != 0755) return false;
    for (const char *dir : kTileFolders)
        if (stat((folder + dir).c_str(), &status) != 0 || (status.st_mode & 0777) != 0755) return false;
    for (const char *file : kTileFiles)
        if (stat((folder + file).c_str(), &status) != 0 || (status.st_mode & 0777) != 0644) return false;
    return true;
}

struct Shadowmount {
    bool answers = false;
    std::string version, why = kNoShadowmount;
    std::map<std::string, bool> titles;   // every title it knows: installed
};

json smp_call(const PwWebuiContext &context, const std::string &route, const std::string &body, std::string *why = nullptr)
{
    std::string reply, problem;
    if (!context.shadowmount || !context.shadowmount(route, body, reply, problem)) {
        if (why) *why = problem.empty() ? kNoShadowmount : problem;
        return nullptr;
    }
    json answer = json::parse(reply, nullptr, false);
    if (why && !answer.is_object()) *why = "The answer on port 10101 is not JSON.";
    return answer.is_object() ? answer : json(nullptr);
}

bool smp_ok(const json &answer) { return answer.is_object() && answer.value("status", -1) == 0; }

std::string smp_failure(const json &answer)
{
    if (!answer.is_object()) return kNoShadowmount;
    const std::string reason = answer.value("error_reason", std::string());
    return answer.value("error", std::string("refused")) + (reason.empty() ? "" : " (" + reason + ")");
}

Shadowmount shadowmount_state(const PwWebuiContext &context)
{
    Shadowmount out;
    const json version = smp_call(context, "/api/v1/version", "{}");
    if (!smp_ok(version)) return out;
    out.version = version.value("shadowmount_version", std::string());
    std::string why;
    const json games = smp_call(context, "/api/v1/games", "{\"include_size\":false}", &why);
    if (!smp_ok(games)) {
        out.why = "Version " + out.version + " did not list its games: " + (why.empty() ? smp_failure(games) : why);
        return out;
    }
    out.answers = true;
    out.why.clear();
    if (games.count("games") && games["games"].is_array())
        for (const json &game : games["games"])
            if (game.is_object()) out.titles[game.value("title_id", std::string())] = game.value("installed", false);
    return out;
}

std::string path_body(const PwWebuiContext &context, const std::string &id)
{
    return json{{"path", tile_folder(context, id)}}.dump();
}

// The tile's param.json: PS5_RetroArch's forwarder tile's, which the console took, with this tile's id and name. Its
// content id is 36 characters, the label after "_00-" 16: one character short, the installer and PlayGo refused it
// ("The size of content id is too small (..., 35)", 0x80b21166; the base PS5, 2026-10-26) and the home screen showed
// the tile without its picture.
std::string tile_param(const std::string &id, const std::string &name)
{
    const std::string number = id.substr(4);
    json param = {
        {"ageLevel", {{"default", 0}}}, {"applicationCategoryType", 0}, {"applicationDrmType", "free"},
        {"attribute", 0}, {"attribute2", 0}, {"attribute3", 524352}, {"conceptId", number}, {"contentBadgeType", 1},
        {"contentId", "UP9000-" + id + "_00-MIHAWKTILE0" + number}, {"contentVersion", "01.000.000"},
        {"downloadDataSize", 256}, {"gameIntent", {{"permittedIntents", json::array({{{"intentType", "launchActivity"}}})}}},
        {"kernel", {{"flexibleMemorySize", 1073741824}}},
        {"localizedParameters", {{"defaultLanguage", "en-US"}, {"en-US", {{"titleName", name}}}}},
        {"masterVersion", "01.00"},
        {"pubtools", {{"creationDate", "2026-10-26 00:00:00"}, {"loudnessSnd0", "-28.00"}, {"toolVersion", "2.00"}}},
        {"requiredSystemSoftwareVersion", "0x0000000000000000"}, {"sdkVersion", "0x0000000000000000"},
        {"titleId", id}, {"versionFileUri", ""}};
    return param.dump(2) + "\n";
}

json tile_json(const PwWebuiContext &context, const std::string &id, const Shadowmount &smp)
{
    std::string text, game;
    read_file(tile_folder(context, id) + "/forward.txt", text, 4096);
    if (text.compare(0, 8, "profile=") == 0) game = text.substr(8, text.find('\n') == std::string::npos ? std::string::npos : text.find('\n') - 8);
    std::string name = id;
    if (read_file(tile_folder(context, id) + "/sce_sys/param.json", text, 64 * 1024)) {
        const json param = json::parse(text, nullptr, false);
        if (param.is_object() && param.count("localizedParameters")) {
            const json &names = param["localizedParameters"];
            const std::string language = names.value("defaultLanguage", std::string("en-US"));
            if (names.count(language) && names[language].is_object()) name = names[language].value("titleName", id);
        }
    }
    const auto known = smp.titles.find(id);
    return json{{"id", id}, {"game", game}, {"name", name}, {"installed", known != smp.titles.end() && known->second},
                {"known", known != smp.titles.end()}, {"pending", exists(tile_folder(context, id) + "/remove.pending")},
                {"adding", exists(tile_folder(context, id) + "/install.pending")}};
}

PwWebuiResponse forwarders(const PwWebuiContext &context)
{
    const Shadowmount smp = shadowmount_state(context);
    json tiles = json::array();
    for (unsigned number = kFirstTile; number <= kLastTile; ++number)
        if (is_tile(context, tile_id(number))) tiles.push_back(tile_json(context, tile_id(number), smp));
    return reply(200, json{{"shadowmount", {{"answers", smp.answers}, {"version", smp.version}, {"why", smp.why}}},
                           {"first", tile_id(kFirstTile)}, {"last", tile_id(kLastTile)}, {"tiles", tiles}});
}

PwWebuiResponse make_forwarder(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    json body;
    if (request.body.size() > (3u << 20)) return fail(413, "The tile's icon is too large.");
    body = json::parse(request.body, nullptr, false);
    if (!body.is_object()) return fail(400, "The tile is not a JSON object.");
    const std::string game = body.value("game", std::string()), name = body.value("name", std::string());
    if (!pw_webui_id(game)) return fail(400, "Choose the game the tile starts.");
    bool listed = false;
    if (context.games)
        for (const PwWebuiGame &entry : context.games()) listed = listed || entry.id == game;
    if (!listed) return fail(404, "That game is not in the library.");
    if (name.empty() || !printable(name, 64)) return fail(400, "The tile's name takes 1 to 64 characters.");
    std::string icon;
    if (body.count("icon") && body["icon"].is_string()) {
        icon = base64_decode(body["icon"].get<std::string>());
        if (icon.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) != 0 || icon.size() > (2u << 20))
            return fail(400, "The tile's icon must be a PNG of up to 2 MiB.");
    }
    for (const char *part : {"/forwarder/eboot.bin", "/forwarder/launch.elf", "/sce_module/libc.prx"})
        if (!exists(context.title_root + part)) return fail(500, std::string("This build has no ") + (part + 1) + ".");
    const Shadowmount smp = shadowmount_state(context);
    if (!smp.answers) return fail(503, smp.why);
    // Ids in turn, from the one after the last made (tiles-next.txt): an id just removed is not taken again soon. The
    // home screen showed a tile again under an id it had installed, removed and installed before without its picture
    // (the base PS5, 2026-10-26), though the system held the picture.
    std::string id, kept;
    read_file(context.data_root + "/tiles-next.txt", kept, 16);
    const unsigned span = kLastTile - kFirstTile + 1, start = unsigned(std::strtoul(kept.c_str(), nullptr, 10));
    for (unsigned step = 0; step < span && id.empty(); ++step) {
        const unsigned number = kFirstTile + ((start >= kFirstTile && start <= kLastTile ? start - kFirstTile : 0) + step) % span;
        if (!exists(tile_folder(context, tile_id(number))) && !smp.titles.count(tile_id(number))) id = tile_id(number);
    }
    if (id.empty()) return fail(507, "Every tile id (" + tile_id(kFirstTile) + " to " + tile_id(kLastTile) + ") is taken.");
    write_file(context.data_root + "/tiles-next.txt", std::to_string(std::stoul(id.substr(4)) + 1) + "\n");
    const std::string folder = tile_folder(context, id);
    const bool made = ensure_dir(context.homebrew_root) && make_dir(folder) && make_dir(folder + "/sce_sys") &&
                      make_dir(folder + "/sce_module") &&
                      copy_file(context.title_root + "/forwarder/eboot.bin", folder + "/eboot.bin") &&
                      copy_file(context.title_root + "/forwarder/launch.elf", folder + "/launch.elf") &&
                      copy_file(context.title_root + "/sce_module/libc.prx", folder + "/sce_module/libc.prx") &&
                      (icon.empty() ? copy_file(context.title_root + "/sce_sys/icon0.png", folder + "/sce_sys/icon0.png")
                                    : write_file(folder + "/sce_sys/icon0.png", icon)) &&
                      write_file(folder + "/sce_sys/param.json", tile_param(id, name)) &&
                      write_file(folder + "/forward.txt", "profile=" + game + "\n") && tile_modes(folder);
    if (!made) {
        remove_tree(folder);
        return fail(507, "The tile could not be written. Check free space.");
    }
    // Registered now, installed by the tiles' worker once MihawkOS has closed (forwarder/worker.c): a tile installed
    // while MihawkOS ran reached the home screen without its picture, as PS5_RetroArch's tiles install once it closes
    const json added = smp_call(context, "/api/v1/manual/add", path_body(context, id));
    if (!smp_ok(added)) {
        remove_tree(folder);
        return reply(200, json{{"id", id}, {"registered", false}, {"why", "The tile was refused: " + smp_failure(added)}});
    }
    std::string why;
    const bool sent = write_file(folder + "/install.pending", "") && context.tile_worker && context.tile_worker(why);
    return reply(200, json{{"id", id}, {"registered", true}, {"worker", sent}, {"why", why}});
}

PwWebuiResponse delete_forwarder(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    json body;
    if (!parse_body(request, body)) return fail(400, "The request is not a JSON object.");
    const std::string id = body.value("id", std::string());
    if (!is_tile(context, id)) return fail(404, "There is no such tile.");
    const Shadowmount smp = shadowmount_state(context);
    if (!smp.answers) return fail(503, smp.why);
    const json removed = smp_call(context, "/api/v1/manual/remove", path_body(context, id));   // first, or a scan adds it again
    if (!smp_ok(removed)) return fail(502, "The tile could not be removed: " + smp_failure(removed));
    const auto known = smp.titles.find(id);
    if (known != smp.titles.end() && known->second &&
        !smp_ok(smp_call(context, "/api/v1/games/uninstall", json{{"title_id", id}}.dump()))) {
        // refused while an app runs, and the launcher is one: the tiles' worker finishes it once MihawkOS has closed
        if (!write_file(tile_folder(context, id) + "/remove.pending", "")) return fail(507, "The tile could not be marked.");
        unlink((tile_folder(context, id) + "/install.pending").c_str());
        std::string why;
        const bool sent = context.tile_worker && context.tile_worker(why);
        return reply(200, json{{"id", id}, {"state", "pending"}, {"worker", sent}, {"why", why}});
    }
    remove_tree(tile_folder(context, id));
    return reply(200, json{{"id", id}, {"state", "removed"}});
}

// A folder the WebUI shows or takes files into: under drive_c's Games (the games) or users (the saves).
bool shared_root(const std::string &path)
{
    return path == "Games" || path.rfind("Games/", 0) == 0 || path == "users" || path.rfind("users/", 0) == 0;
}

PwWebuiResponse files(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    std::string prefix = arg(request, "prefix"), relative = arg(request, "path"), folder;
    if (prefix.empty()) prefix = "mihawk";
    if (relative.empty()) relative = "users";
    if (!shared_root(relative) || !drive_path(context, prefix, relative, folder)) return fail(404, "No such folder.");
    json dirs = json::array(), list = json::array();
    for (const Entry &entry : list_dir(folder))
        if (entry.dir) dirs.push_back(entry.name);
        else list.push_back(json{{"name", entry.name}, {"size", entry.size}});
    std::string windows = "C:\\" + relative;
    std::replace(windows.begin(), windows.end(), '/', '\\');
    return reply(200, json{{"prefix", prefix}, {"path", relative}, {"windows", windows}, {"dirs", dirs}, {"files", list}});
}

PwWebuiResponse prefixes(const PwWebuiContext &context)
{
    json list = json::array();
    for (const Entry &entry : list_dir(context.data_root + "/prefixes"))
        if (entry.dir && pw_webui_id(entry.name) && is_dir(context.data_root + "/prefixes/" + entry.name + "/drive_c"))
            list.push_back(entry.name);
    return reply(200, json{{"prefixes", list}});
}

PwWebuiResponse one_file(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    std::string prefix = arg(request, "prefix"), relative = arg(request, "path"), folder;
    if (prefix.empty()) prefix = "mihawk";
    const size_t slash = relative.rfind('/');
    if (slash == std::string::npos || !shared_root(relative)) return fail(404, "No such file.");
    const std::string name = relative.substr(slash + 1);
    struct stat status;
    if (!pw_webui_part(name) || !drive_path(context, prefix, relative.substr(0, slash), folder) ||
        lstat((folder + "/" + name).c_str(), &status) != 0 || !S_ISREG(status.st_mode))
        return fail(404, "No such file.");
    PwWebuiResponse response;
    response.type = "application/octet-stream";
    response.file = folder + "/" + name;
    response.download = name;
    return response;
}

void plan_folder(const std::string &folder, const std::string &name, int depth, std::vector<PwWebuiZipFile> &out,
                 unsigned long long &total, bool &over)
{
    if (depth > 16 || over) return;
    for (const Entry &entry : list_dir(folder)) {
        if (over) return;
        const std::string path = folder + "/" + entry.name, inner = name + "/" + entry.name;
        if (entry.dir) {
            plan_folder(path, inner, depth + 1, out, total, over);
            continue;
        }
        struct stat status;
        if (lstat(path.c_str(), &status) != 0) continue;
        total += (unsigned long long)entry.size;
        out.push_back({inner, path, (unsigned long long)entry.size, (long long)status.st_mtime, {}});
        over = out.size() > 65000 || total >= 0xfff00000ull;
    }
}

PwWebuiResponse upload_status(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    std::string absolute, why;
    if (!pw_webui_upload_path(context, arg(request, "prefix").empty() ? "mihawk" : arg(request, "prefix"),
                              arg(request, "path"), &absolute, &why))
        return fail(400, why);
    struct stat status;
    if (lstat(absolute.c_str(), &status) == 0 && S_ISREG(status.st_mode))
        return reply(200, json{{"done", true}, {"have", (long long)status.st_size}});
    const long long have = lstat((absolute + ".part").c_str(), &status) == 0 && S_ISREG(status.st_mode) ? status.st_size : 0;
    return reply(200, json{{"done", false}, {"have", have}});
}

PwWebuiResponse status(const PwWebuiContext &context)
{
    std::string stamp;
    read_file(context.title_root + "/runtime-stamp", stamp, 128);
    stamp.erase(std::remove(stamp.begin(), stamp.end(), '\n'), stamp.end());
    // The language the guided setup kept (src/pw_setup.h: a "language=<tag>" line): the page's own, unless its viewer chose
    std::string setup, language;
    read_file(context.data_root + "/setup.txt", setup, 4096);
    for (size_t at = 0; at < setup.size();) {
        const size_t end = std::min(setup.find('\n', at), setup.size());
        if (setup.compare(at, 9, "language=") == 0) language = setup.substr(at + 9, std::min<size_t>(end - at - 9, 15));
        at = end + 1;
    }
    if (!pw_webui_id(language)) language.clear();
    return reply(200, json{{"token", context.token}, {"title", "MihawkOS"}, {"version", context.version},
                           {"runtime", stamp.substr(0, 12)}, {"data", is_dir(context.data_root)}, {"language", language}});
}

}  // namespace

bool pw_webui_upload_path(const PwWebuiContext &context, const std::string &prefix, const std::string &path,
                          std::string *absolute, std::string *why)
{
    std::string folder = context.data_root + "/prefixes/" + prefix + "/drive_c";
    if (!pw_webui_id(prefix) || !is_dir(folder)) return *why = "No such prefix.", false;
    std::vector<std::string> parts;
    for (size_t start = 0;;) {
        const size_t slash = path.find('/', start);
        parts.push_back(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    if (parts.size() > 24 || !((parts[0] == "Games" && parts.size() >= 3) || (parts[0] == "users" && parts.size() >= 4)))
        return *why = "Files go into C:\\Games\\<folder> or a folder of C:\\users\\<user>.", false;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (!pw_webui_part(parts[i])) return *why = "A part of the path is not allowed: " + parts[i], false;
        folder += "/" + parts[i];
        if (i + 1 == parts.size()) break;
        struct stat status;
        if (lstat(folder.c_str(), &status) != 0) {
            if (!make_dir(folder)) return *why = "A folder cannot be made.", false;
        } else if (!S_ISDIR(status.st_mode)) {
            return *why = "A part of the path is a file or a link: " + parts[i], false;
        }
    }
    struct stat status;
    if (lstat(folder.c_str(), &status) == 0 && !S_ISREG(status.st_mode)) return *why = "The target is not a file.", false;
    *absolute = folder;
    return true;
}

bool pw_webui_zip_plan(const PwWebuiContext &context, const std::string &prefix, const std::string &path,
                       std::vector<PwWebuiZipFile> *out, std::string *why)
{
    std::string folder;
    if (!shared_root(path) || path == "Games" || path == "users" || !drive_path(context, prefix, path, folder))
        return *why = "No such folder.", false;
    unsigned long long total = 0;
    bool over = false;
    out->clear();
    plan_folder(folder, path, 0, *out, total, over);   // named from C:\, so a backup put back lands where it was
    if (over) return *why = "This folder is too large for one ZIP (65000 files or 4 GiB): choose a folder inside it.", false;
    return true;
}

void pw_webui_report_plan(const PwWebuiContext &context, std::vector<PwWebuiZipFile> *files)
{
    const long long now = time(nullptr);
    std::string stamp;
    read_file(context.title_root + "/runtime-stamp", stamp, 128);
    stamp.erase(std::remove(stamp.begin(), stamp.end(), '\n'), stamp.end());
    files->clear();
    files->push_back({"about.txt", "", 0, now,
                      "MihawkOS " + context.version + "\nruntime " + (stamp.empty() ? "not installed" : stamp.substr(0, 12)) +
                          "\nsettings " + settings_json(context).dump() + "\n"});
    size_t total = 0;
    for (const Entry &log : log_files(context)) {
        std::string text;
        bool cut = false;
        if (total >= (16u << 20) || !read_tail(context.data_root + "/logs/" + log.name, text, kLogLimit, &cut)) continue;
        struct stat status{};
        stat((context.data_root + "/logs/" + log.name).c_str(), &status);
        files->push_back({"logs/" + log.name, "", 0, (long long)status.st_mtime,
                          (cut ? std::string("[... the start is left out: the last 512 KiB follow ...]\n") : std::string()) +
                              pw_webui_mask(text)});
        total += files->back().text.size();
    }
    for (PwWebuiZipFile &file : *files) file.size = file.text.size();
}

bool pw_webui_id(const std::string &id)
{
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char c : id)
        if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}

bool pw_webui_part(const std::string &part)
{
    if (part.empty() || part.size() > 255 || part[0] == '.' || !printable(part, 255)) return false;
    return part.find_first_of("/\\:") == std::string::npos;
}

std::string pw_webui_mask(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        // An address: four groups of 1-3 digits joined by dots, not inside a longer run of digits and dots
        size_t j = i, groups = 0;
        const bool start = i == 0 || !(std::isdigit((unsigned char)text[i - 1]) || text[i - 1] == '.');
        while (start && groups < 4) {
            size_t digits = 0;
            while (j < text.size() && std::isdigit((unsigned char)text[j]) && digits < 4) ++j, ++digits;
            if (digits == 0 || digits > 3) break;
            ++groups;
            if (groups < 4) {
                if (j >= text.size() || text[j] != '.') break;
                ++j;
            }
        }
        if (start && groups == 4 && (j >= text.size() || !(std::isdigit((unsigned char)text[j]) || text[j] == '.'))) {
            out += "<address>";
            i = j;
        } else {
            out += text[i++];
        }
    }
    return out;
}

std::string pw_webui_asset(const PwWebuiContext &context, const std::string &path, std::string *type)
{
    static const struct { const char *path, *file, *type; } assets[] = {
        {"/", "webui/index.html", "text/html; charset=utf-8"},
        {"/index.html", "webui/index.html", "text/html; charset=utf-8"},
        {"/app.css", "webui/app.css", "text/css; charset=utf-8"},
        {"/app.js", "webui/app.js", "text/javascript; charset=utf-8"},
        {"/i18n.js", "webui/i18n.js", "text/javascript; charset=utf-8"},
        // The title's own files: its icon and the launcher's Inter
        {"/icon.png", "sce_sys/icon0.png", "image/png"},
        {"/inter.ttf", "assets/hui/fonts/Inter-Regular.ttf", "font/ttf"},
        {"/inter-semibold.ttf", "assets/hui/fonts/Inter-SemiBold.ttf", "font/ttf"},
        // The language picker's flags: the guided setup's (ps5/assets/flags, lipis/flag-icons), one per language it has
        {"/flags/us.svg", "webui/flags/us.svg", "image/svg+xml"}, {"/flags/fr.svg", "webui/flags/fr.svg", "image/svg+xml"},
        {"/flags/es.svg", "webui/flags/es.svg", "image/svg+xml"}, {"/flags/de.svg", "webui/flags/de.svg", "image/svg+xml"},
        {"/flags/it.svg", "webui/flags/it.svg", "image/svg+xml"}, {"/flags/ru.svg", "webui/flags/ru.svg", "image/svg+xml"},
        {"/flags/cn.svg", "webui/flags/cn.svg", "image/svg+xml"}, {"/flags/jp.svg", "webui/flags/jp.svg", "image/svg+xml"},
        {"/flags/br.svg", "webui/flags/br.svg", "image/svg+xml"}, {"/flags/pl.svg", "webui/flags/pl.svg", "image/svg+xml"},
    };
    for (const auto &asset : assets)
        if (path == asset.path) {
            if (type) *type = asset.type;
            return context.title_root + "/" + asset.file;
        }
    return {};
}

PwWebuiResponse pw_webui_handle(const PwWebuiContext &context, const PwWebuiRequest &request)
{
    const std::string &path = request.path, &method = request.method;
    const bool get = method == "GET" || method == "HEAD", post = method == "POST";
    if (path.rfind("/api/", 0) != 0) {
        std::string type;
        const std::string file = pw_webui_asset(context, path, &type);
        if (!get || file.empty() || !exists(file)) return fail(404, "Not found.");
        PwWebuiResponse response;
        response.type = type;
        response.file = file;
        return response;
    }
    if (!get && !token_ok(context, request)) return fail(403, "Reload the page: its session is from an earlier start.");
    if (get && path == "/api/status") return status(context);
    if (get && path == "/api/library") return library(context);
    if (post && path == "/api/library/game") return save_game(context, request);
    if (post && path == "/api/library/delete") return delete_game(context, request);
    if (get && path == "/api/browse") return browse(context, request);
    if (get && path == "/api/settings") return reply(200, settings_json(context));
    if (post && path == "/api/settings") return save_settings(context, request);
    if (get && path == "/api/logs") return logs(context);
    if (get && path == "/api/log") return log_text(context, request);
    if (get && path == "/api/upload/status") return upload_status(context, request);
    if (get && path == "/api/files") return files(context, request);
    if (get && path == "/api/prefixes") return prefixes(context);
    if (get && path == "/api/file") return one_file(context, request);
    if (post && path == "/api/media/search") return media_search(context, request);
    if (get && path == "/api/media/thumb") return media_thumb(context, request);
    if (post && path == "/api/media/apply") return media_apply(context, request);
    if ((get || method == "PUT") && path == "/api/media/cover") return media_cover(context, request);
    if (post && path == "/api/media/remove") return media_remove(context, request);
    if (get && path == "/api/forwarders") return forwarders(context);
    if (post && path == "/api/forwarders") return make_forwarder(context, request);
    if (post && path == "/api/forwarders/delete") return delete_forwarder(context, request);
    if (get && path == "/api/forwarders/icon") {
        const std::string id = arg(request, "id");
        std::string data;
        if (!is_tile(context, id) || !read_file(tile_folder(context, id) + "/sce_sys/icon0.png", data, 4u << 20) ||
            !image_bytes(data))
            return fail(404, "No such icon.");
        PwWebuiResponse response;
        response.type = image_type(data);
        response.body = data;
        return response;
    }
    return fail(404, "No such request.");
}

void pw_webui_forwarders_finish(const PwWebuiContext &context, const std::function<void(const std::string &)> &say)
{
    size_t waiting = 0;
    for (unsigned number = kFirstTile; number <= kLastTile; ++number) {
        if (!is_tile(context, tile_id(number))) continue;
        // A tile made with other modes is mended (tile_modes)
        const std::string folder = tile_folder(context, tile_id(number));
        if (!tile_modes_right(folder))
            say("forwarders: " + tile_id(number) + (tile_modes(folder) ? " modes mended" : " modes could not be mended"));
        if (exists(folder + "/remove.pending") || exists(folder + "/install.pending")) ++waiting;
    }
    if (!waiting) return;
    // installs and removals wait for MihawkOS to close: the tiles' worker finishes them (a console restart ended the last)
    std::string why;
    say("forwarders: " + std::to_string(waiting) + " tile(s) wait for MihawkOS to close; the tiles' worker " +
        (context.tile_worker && context.tile_worker(why) ? std::string("is sent") : "could not be sent: " + why));
}
