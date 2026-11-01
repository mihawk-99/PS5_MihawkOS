// SPDX-License-Identifier: GPL-3.0-or-later
// The WebUI's routes (src/pw_webui.hpp) on a data folder of their own: the token, the library and its entries, the
// executable picker, the settings, the logs (addresses masked), the page's files, and every way out of the folders
// refused.
#include "../src/pw_webui.hpp"
#include "json.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using json = nlohmann::json;

static void put(const std::string &path, const std::string &text)
{
    std::ofstream(path) << text;
}

static std::string get(const std::string &path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static PwWebuiResponse call(const PwWebuiContext &context, const char *method, const std::string &path,
                            const std::string &body = "", std::map<std::string, std::string> args = {},
                            const std::string &token = "token-1")
{
    PwWebuiRequest request;
    request.method = method;
    request.path = path;
    request.body = body;
    request.args = std::move(args);
    request.token = token;
    return pw_webui_handle(context, request);
}

int main()
{
    char folder[] = "/tmp/pw-webui-XXXXXX";
    assert(mkdtemp(folder));
    const std::string root = folder, data = root + "/data", title = root + "/title";
    for (const std::string &dir : {data, title, title + "/webui", data + "/games", data + "/logs", data + "/logs/sessions",
                                   data + "/logs/sessions/0", data + "/prefixes", data + "/prefixes/mihawk",
                                   data + "/prefixes/mihawk/drive_c", data + "/prefixes/mihawk/drive_c/Games",
                                   data + "/prefixes/mihawk/drive_c/Games/Shooter"})
        assert(mkdir(dir.c_str(), 0755) == 0);
    put(title + "/webui/index.html", "<!doctype html>");
    put(title + "/runtime-stamp", "c8a3443d83d0aaaa\n");
    put(data + "/prefixes/mihawk/drive_c/Games/Shooter/Shooter.EXE", "MZ");
    put(data + "/prefixes/mihawk/drive_c/Games/Shooter/readme.txt", "");
    put(data + "/logs/title-last.log", "launch at 198.51.100.20:6767 and 203.0.113.1, version 1.2.3, 1.2.3.4.5 kept\n");
    put(data + "/logs/sessions/0/wine.log", "ok\n");
    symlink("/etc", (data + "/prefixes/mihawk/drive_c/Games/link").c_str());

    std::vector<std::string> changes;
    PwWebuiContext context;
    context.data_root = data;
    context.title_root = title;
    context.token = "token-1";
    context.version = "test";
    context.games = []() {
        PwWebuiGame game;
        game.id = "mihawk";
        game.name = "Mihawk OS";
        game.complaint = "";
        return std::vector<PwWebuiGame>{game};
    };
    context.changed = [&changes](const char *what) { changes.push_back(what); };

    // Status gives the token; a write without it is refused
    auto r = call(context, "GET", "/api/status");
    assert(r.status == 200 && json::parse(r.body)["token"] == "token-1" && json::parse(r.body)["runtime"] == "c8a3443d83d0");
    assert(json::parse(r.body)["language"] == "");
    put(data + "/setup.txt", "done=1\nlanguage=fr-FR\n");
    assert(json::parse(call(context, "GET", "/api/status").body)["language"] == "fr-FR");
    put(data + "/setup.txt", "language=../x\n");
    assert(json::parse(call(context, "GET", "/api/status").body)["language"] == "");
    assert(call(context, "POST", "/api/settings", "{\"game_mode\":false}", {}, "stale").status == 403);
    assert(call(context, "POST", "/api/settings", "{\"game_mode\":false}", {}, "").status == 403);

    // Settings: defaults, then each saved through the launcher's own files
    r = call(context, "GET", "/api/settings");
    json settings = json::parse(r.body);
    assert(settings["high_refresh"] == true);
    assert(call(context, "POST", "/api/settings", R"({"high_refresh":false})").status == 200 &&
           get(data + "/high-refresh.txt") == "120hz off\n");
    assert(call(context, "POST", "/api/settings", R"({"high_refresh":"no"})").status == 400);
    assert(settings["game_mode"] == true && settings["controller"] == "xbox-360" && settings["logging"] == false &&
           settings["show_all"] == false && settings["mouse"]["left"] == "touchpad");
    r = call(context, "POST", "/api/settings",
             R"({"game_mode":false,"controller":"dualsense","logging":true,"show_all":true,"mouse":{"right":"touchpad"}})");
    assert(r.status == 200);
    settings = json::parse(r.body);
    assert(settings["game_mode"] == false && settings["controller"] == "dualsense" && settings["logging"] == true &&
           settings["show_all"] == true);
    assert(settings["mouse"]["right"] == "touchpad" && settings["mouse"]["left"] == "none");  // one action a button
    assert(get(data + "/controller.txt") == "controller dualsense\n" && get(data + "/game-mode.txt") == "game-mode off\n");
    assert(access((data + "/library-show-all").c_str(), F_OK) == 0 && changes.back() == "settings");
    assert(call(context, "POST", "/api/settings", R"({"controller":"xbox-series","game_mode":true})").status == 400);
    assert(get(data + "/game-mode.txt") == "game-mode off\n");  // a bad value saves nothing
    assert(call(context, "POST", "/api/settings", R"({"mouse":{"left":"nose"}})").status == 400);
    assert(call(context, "POST", "/api/settings", "not json").status == 400);
    assert(call(context, "POST", "/api/settings", R"({"show_all":false})").status == 200 &&
           access((data + "/library-show-all").c_str(), F_OK) != 0);

    // The on-screen keyboard's gesture: Off, four choices, saved only when chosen, and a bad one saves nothing
    r = call(context, "GET", "/api/settings");
    settings = json::parse(r.body);
    assert(settings["keyboard"] == "off" && settings["choices"]["keyboards"].size() == 4 &&
           settings["choices"]["keyboards"][2]["id"] == "l3-r3");
    r = call(context, "POST", "/api/settings", R"({"keyboard":"l3-r3"})");
    assert(r.status == 200 && json::parse(r.body)["keyboard"] == "l3-r3" && json::parse(r.body)["mouse"]["right"] == "touchpad");
    assert(get(data + "/controls.txt").find("keyboard l3-r3\n") != std::string::npos);
    assert(call(context, "POST", "/api/settings", R"({"keyboard":"nose"})").status == 400);
    assert(call(context, "POST", "/api/settings", R"({"keyboard":3})").status == 400);
    assert(json::parse(call(context, "GET", "/api/settings").body)["keyboard"] == "l3-r3");   // a bad value saved nothing
    assert(call(context, "POST", "/api/settings", R"({"keyboard":"off"})").status == 200 &&
           get(data + "/controls.txt").find("keyboard") == std::string::npos);

    // The theme and the music: kept by id in the title folder's hui/; the first is what nothing kept means
    context.themes = {{"gloss", "Gloss"}, {"aero", "Aero"}};
    context.music = {{"bubble-garden", "Bubble Garden"}, {"sky-lounge", "Sky Lounge"}};
    settings = json::parse(call(context, "GET", "/api/settings").body);
    assert(settings["theme"] == "gloss" && settings["music"] == "bubble-garden" &&
           settings["choices"]["music"].size() == 3 && settings["choices"]["music"][2]["id"] == "off");
    assert(call(context, "POST", "/api/settings", R"({"theme":"aero","music":"off"})").status == 200);
    assert(get(title + "/hui/theme.txt") == "aero\n" && get(title + "/hui/music.txt") == "off\n");
    assert(call(context, "POST", "/api/settings", R"({"theme":"off"})").status == 400);
    assert(call(context, "POST", "/api/settings", R"({"music":"../x","game_mode":true})").status == 400);
    assert(get(data + "/game-mode.txt") == "game-mode off\n");
    settings = json::parse(call(context, "GET", "/api/settings").body);
    assert(settings["theme"] == "aero" && settings["music"] == "off");

    // Library entries: create, keys the page does not know kept, delete
    put(data + "/games/keepme.json", "");
    r = call(context, "POST", "/api/library/game",
             R"({"id":"shooter","name":"Shooter","detail":"A game","executable":"C:\\Games\\Shooter\\Shooter.EXE",
                 "env":["WINE_PS5_OPENGL=1"],"desktop":false,"launcher":false})");
    assert(r.status == 200 && changes.back() == "library");
    json entry = json::parse(get(data + "/games/shooter/game.json"));
    assert(entry["name"] == "Shooter" && entry["env"][0] == "WINE_PS5_OPENGL=1" && entry["desktop"] == false &&
           entry["launcher"] == false &&
           !entry.count("arguments") && !entry.count("prefix"));
    put(data + "/games/shooter/game.json", R"({"name":"Old","executable":"C:\\x.exe","future":7})");
    assert(call(context, "POST", "/api/library/game",
                R"({"id":"shooter","name":"New","executable":"C:\\Games\\Shooter\\Shooter.EXE","desktop":true})").status == 200);
    entry = json::parse(get(data + "/games/shooter/game.json"));
    assert(entry["name"] == "New" && entry["future"] == 7 && !entry.count("desktop") && !entry.count("launcher"));
    struct stat opened{};   // open to every process, as the FTP server's copies are
    assert(stat((data + "/games/shooter").c_str(), &opened) == 0 && (opened.st_mode & 0777) == 0777 &&
           stat((data + "/games/shooter/game.json").c_str(), &opened) == 0 && (opened.st_mode & 0777) == 0666);
    for (const char *bad : {R"({"id":"../x","name":"A","executable":"C:\\a.exe"})",
                            R"({"id":"a","name":"","executable":"C:\\a.exe"})",
                            R"({"id":"a","name":"A","executable":"/etc/passwd"})",
                            R"({"id":"a","name":"A","executable":"C:\\Games\\..\\..\\x.exe"})",
                            R"({"id":"a","name":"A","executable":"C:\\a.exe","env":["lower=1"]})",
                            R"({"id":"a","name":"A","executable":"C:\\a.exe","prefix":"../x"})",
                            R"({"id":"a","name":"A\u0001","executable":"C:\\a.exe"})"})
        assert(call(context, "POST", "/api/library/game", bad).status == 400);
    assert(call(context, "POST", "/api/library/delete", R"({"id":"../games"})").status == 400);
    assert(call(context, "POST", "/api/library/delete", R"({"id":"nothing"})").status == 404);
    assert(call(context, "POST", "/api/library/delete", R"({"id":"shooter"})").status == 200);
    assert(access((data + "/games/shooter").c_str(), F_OK) != 0 && access((data + "/games").c_str(), F_OK) == 0);
    r = call(context, "GET", "/api/library");
    assert(r.status == 200 && json::parse(r.body)["games"][0]["id"] == "mihawk");

    // The executable picker: folders and .exe files, never a symlink or a way out
    r = call(context, "GET", "/api/browse", "", {{"path", "Games/Shooter"}});
    json listing = json::parse(r.body);
    assert(r.status == 200 && listing["exes"].size() == 1 && listing["exes"][0]["name"] == "Shooter.EXE" &&
           listing["windows"] == "C:\\Games\\Shooter");
    r = call(context, "GET", "/api/browse", "", {});
    listing = json::parse(r.body);
    assert(listing["dirs"].size() == 1 && listing["dirs"][0] == "Shooter");  // the symlink is not listed
    for (const char *bad : {"Games/..", "Games/../..", "Games/link", "../data", "Games//Shooter", "/etc"})
        assert(call(context, "GET", "/api/browse", "", {{"path", bad}}).status == 404);
    assert(call(context, "GET", "/api/browse", "", {{"prefix", "../x"}}).status == 404);

    // Logs: listed one level into the sessions, addresses masked, never a way out
    r = call(context, "GET", "/api/logs");
    assert(r.status == 200 && json::parse(r.body)["logs"].size() == 2);
    r = call(context, "GET", "/api/log", "", {{"name", "title-last.log"}});
    assert(r.status == 200 && r.body.find("198.51.100") == std::string::npos && r.body.find("203.0.113") == std::string::npos && r.body.find("<address>:6767") != std::string::npos &&
           r.body.find("version 1.2.3,") != std::string::npos && r.body.find("1.2.3.4.5") != std::string::npos);
    assert(call(context, "GET", "/api/log", "", {{"name", "sessions/0/wine.log"}}).status == 200);
    for (const char *bad : {"../game-mode.txt", "sessions/../../game-mode.txt", "", ".hidden", "sessions"})
        assert(call(context, "GET", "/api/log", "", {{"name", bad}}).status == 404);
    std::vector<PwWebuiZipFile> report;
    pw_webui_report_plan(context, &report);
    assert(report.size() == 3 && report[0].name == "about.txt" && report[0].path.empty() &&
           report[0].text.find("runtime c8a3443d83d0") != std::string::npos && report[0].size == report[0].text.size());
    for (const PwWebuiZipFile &file : report) assert(file.text.find("198.51.100") == std::string::npos);

    // The page's files: only those on the list
    r = call(context, "GET", "/");
    assert(r.status == 200 && r.file == title + "/webui/index.html" && r.type.rfind("text/html", 0) == 0);
    assert(call(context, "GET", "/app.js").status == 404);   // listed, but not there
    assert(call(context, "GET", "/../runtime-stamp").status == 404 && call(context, "GET", "/webui/index.html").status == 404);
    assert(call(context, "POST", "/").status == 404 && call(context, "GET", "/api/nothing").status == 404);

    // Uploads: only under C:\Games\<folder>, folders made, never a way out; the status sees a .part
    std::string target, why;
    assert(pw_webui_upload_path(context, "mihawk", "Games/New/data/level.pak", &target, &why) &&
           target == data + "/prefixes/mihawk/drive_c/Games/New/data/level.pak");
    struct stat made;
    assert(lstat((data + "/prefixes/mihawk/drive_c/Games/New/data").c_str(), &made) == 0 && S_ISDIR(made.st_mode));
    assert(pw_webui_upload_path(context, "mihawk", "users/Mihawk/Saved Games/save.dat", &target, &why));
    assert(!pw_webui_upload_path(context, "mihawk", "users/Mihawk/x", &target, &why));   // a folder of the user, not its root
    for (const char *bad : {"Games/x", "Windows/system32/x.dll", "Games/../x/y", "Games/link/x", "Games/New/.hidden",
                            "Games/New//x", "Games/New/a:b"})
        assert(!pw_webui_upload_path(context, "mihawk", bad, &target, &why));
    assert(!pw_webui_upload_path(context, "../x", "Games/New/x", &target, &why));
    put(data + "/prefixes/mihawk/drive_c/Games/New/data/level.pak.part", "12345");
    r = call(context, "GET", "/api/upload/status", "", {{"path", "Games/New/data/level.pak"}});
    assert(r.status == 200 && json::parse(r.body)["have"] == 5 && json::parse(r.body)["done"] == false);

    // Media: the store's answers canned; the search, a thumbnail fetched once, the art and details applied, your own
    // picture, refusals
    const std::string jpeg = std::string("\xff\xd8\xff\xe0", 4) + std::string(100, 'j'), png = std::string("\x89PNG\r\n\x1a\n", 8) + "p";
    std::vector<std::string> fetched;
    context.fetch = [&](const std::string &url, size_t, std::string &body, std::string &why) {
        fetched.push_back(url);
        if (url.find("storesearch/?term=Night%20walk") != std::string::npos)
            return body = R"({"total":2,"items":[{"type":"app","name":"Nightwalk","id":4242},{"type":"app","name":"Bad"}]})", true;
        if (url.find("/4242/capsule_231x87.jpg") != std::string::npos || url.find("/4242/header.jpg") != std::string::npos)
            return body = jpeg, true;
        if (url.find("appdetails?appids=4242") != std::string::npos)
            return body = R"({"4242":{"success":true,"data":{"name":"Nightwalk","short_description":"A &quot;walk&quot; &amp; <b>more</b>.","developers":["Dev"],"release_date":{"date":"1 Jan, 2020"},"genres":[{"description":"Action"}]}}})", true;
        return why = "404", false;
    };
    r = call(context, "POST", "/api/media/search", R"({"name":"Night walk"})");
    assert(r.status == 200 && json::parse(r.body)["matches"].size() == 1 && json::parse(r.body)["matches"][0]["appid"] == "4242");
    r = call(context, "GET", "/api/media/thumb", "", {{"appid", "4242"}});
    assert(r.status == 200 && r.body == jpeg && r.type == "image/jpeg");
    const size_t before = fetched.size();
    assert(call(context, "GET", "/api/media/thumb", "", {{"appid", "4242"}}).body == jpeg && fetched.size() == before);  // kept
    assert(call(context, "GET", "/api/media/thumb", "", {{"appid", "../x"}}).status == 404);
    assert(call(context, "POST", "/api/library/game", R"({"id":"night","name":"Nightwalk","executable":"C:\\Games\\N\\n.exe"})").status == 200);
    r = call(context, "POST", "/api/media/apply", R"({"id":"night","appid":"4242"})");
    assert(r.status == 200 && json::parse(r.body)["portrait"] == false && json::parse(r.body)["meta"]["release"] == "1 Jan, 2020");
    assert(get(data + "/covers/night-header.img") == jpeg);
    assert(json::parse(get(data + "/games/night/meta.json"))["genres"][0] == "Action");
    assert(json::parse(get(data + "/games/night/meta.json"))["description"] == "A \"walk\" & more.");
    assert(call(context, "POST", "/api/media/apply", R"({"id":"nothing","appid":"4242"})").status == 404);
    assert(call(context, "POST", "/api/media/apply", R"({"id":"night","appid":"77"})").status == 502);   // no art
    r = call(context, "GET", "/api/media/cover", "", {{"id", "night"}});
    assert(r.status == 200 && r.body == jpeg);
    assert(call(context, "PUT", "/api/media/cover", png, {{"id", "night"}, {"kind", "portrait"}}).status == 200);
    assert(call(context, "GET", "/api/media/cover", "", {{"id", "night"}, {"kind", "portrait"}}).type == "image/png");
    assert(call(context, "PUT", "/api/media/cover", "<svg/>", {{"id", "night"}}).status == 400);
    assert(call(context, "PUT", "/api/media/cover", png, {{"id", "night"}, {"kind", "../../x"}}).status == 404);
    assert(call(context, "PUT", "/api/media/cover", png, {{"id", "night"}}, "stale").status == 403);
    assert(call(context, "POST", "/api/media/remove", R"({"id":"night"})").status == 200 &&
           access((data + "/covers/night-header.img").c_str(), F_OK) != 0 && access((data + "/games/night/meta.json").c_str(), F_OK) != 0);

    // Forwarders: a tile made in MihawkOS's own range, registered through a ShadowMountPlus that answers; listed;
    // removed, or marked when the uninstall is refused, and finished at the next start
    for (const std::string &dir : {title + "/forwarder", title + "/sce_module", title + "/sce_sys", root + "/homebrew"})
        assert(mkdir(dir.c_str(), 0755) == 0);
    put(title + "/forwarder/eboot.bin", "TILE");
    put(title + "/forwarder/launch.elf", "ELF");
    put(title + "/sce_module/libc.prx", "PRX");
    put(title + "/sce_sys/icon0.png", png);
    context.homebrew_root = root + "/homebrew";
    std::map<std::string, bool> installed;
    std::vector<std::string> smp_calls;
    bool smp_up = true, uninstall_ok = false;
    context.shadowmount = [&](const std::string &route, const std::string &body, std::string &reply, std::string &why) {
        smp_calls.push_back(route + " " + body);
        if (!smp_up) return why = "no connection", false;
        if (route == "/api/v1/version") return reply = R"({"status":0,"shadowmount_version":"1.7"})", true;
        if (route == "/api/v1/games") {
            json games = json::array();
            for (const auto &[id, on] : installed) games.push_back(json{{"title_id", id}, {"installed", on}});
            return reply = json{{"status", 0}, {"games", games}}.dump(), true;
        }
        if (route == "/api/v1/manual/add") return reply = R"({"status":0})", true;
        if (route == "/api/v1/scan") {
            for (const char *id : {"PPSA99300", "PPSA99301"})
                if (access((root + "/homebrew/" + id).c_str(), F_OK) == 0) installed[id] = true;
            return reply = R"({"status":0})", true;
        }
        if (route == "/api/v1/manual/remove") return reply = R"({"status":0})", true;
        if (route == "/api/v1/games/uninstall")
            return reply = uninstall_ok ? R"({"status":0})" : R"({"status":1,"error":"busy","error_reason":"an app runs"})", true;
        return why = "unknown route", false;
    };
    installed["PPSA99300"] = false;   // ShadowMountPlus knows this id already: the next free one is taken
    int workers = 0;   // the tiles' worker sent to the ELF loader (forwarder/worker.c)
    context.tile_worker = [&workers](std::string &) { ++workers; return true; };
    std::string icon_b64 = "iVBORw0KGgpw";   // the PNG signature and "p", in base64
    r = call(context, "POST", "/api/forwarders", json{{"game", "mihawk"}, {"name", "Mihawk Desktop"}, {"icon", icon_b64}}.dump());
    assert(r.status == 200 && json::parse(r.body)["id"] == "PPSA99301" && json::parse(r.body)["registered"] == true &&
           json::parse(r.body)["worker"] == true && workers == 1);
    const std::string tile = root + "/homebrew/PPSA99301";
    // registered, not installed: the worker installs it once MihawkOS has closed, never a scan while it runs
    assert(access((tile + "/install.pending").c_str(), F_OK) == 0 && !installed.count("PPSA99301"));
    for (const std::string &asked : smp_calls) assert(asked.rfind("/api/v1/scan", 0) != 0);
    assert(json::parse(call(context, "GET", "/api/forwarders").body)["tiles"][0]["adding"] == true);
    installed["PPSA99301"] = true;   // as the worker leaves it
    unlink((tile + "/install.pending").c_str());
    assert(get(tile + "/forward.txt") == "profile=mihawk\n" && get(tile + "/eboot.bin") == "TILE" &&
           get(tile + "/sce_module/libc.prx") == "PRX" && get(tile + "/sce_sys/icon0.png") == png);
    struct stat mode{};
    for (const char *program : {"/eboot.bin", "/launch.elf", "/sce_module/libc.prx"})   // the system starts it (errno 13 when not)
        assert(stat((tile + program).c_str(), &mode) == 0 && (mode.st_mode & 0777) == 0755);
    for (const char *file : {"/sce_sys/param.json", "/sce_sys/icon0.png", "/forward.txt"})   // the picture showed so
        assert(stat((tile + file).c_str(), &mode) == 0 && (mode.st_mode & 0777) == 0644);
    for (const char *dir : {"", "/sce_sys", "/sce_module"})
        assert(stat((tile + dir).c_str(), &mode) == 0 && (mode.st_mode & 0777) == 0755);
    json param = json::parse(get(tile + "/sce_sys/param.json"));
    assert(param["contentId"] == "UP9000-PPSA99301_00-MIHAWKTILE099301" && param["contentId"].get<std::string>().size() == 36);
    assert(param["titleId"] == "PPSA99301" && param["localizedParameters"]["en-US"]["titleName"] == "Mihawk Desktop");
    r = call(context, "GET", "/api/forwarders");
    json tiles = json::parse(r.body);
    assert(tiles["shadowmount"]["answers"] == true && tiles["tiles"].size() == 1 && tiles["tiles"][0]["game"] == "mihawk" &&
           tiles["tiles"][0]["installed"] == true && tiles["first"] == "PPSA99300");
    for (const char *bad : {R"({"game":"nothing","name":"X"})", R"({"game":"mihawk","name":""})",
                            R"({"game":"mihawk","name":"X","icon":"PHN2Zz4="})", R"({"game":"../x","name":"X"})"})
        assert(call(context, "POST", "/api/forwarders", bad).status >= 400);
    smp_up = false;
    assert(call(context, "POST", "/api/forwarders", R"({"game":"mihawk","name":"Second"})").status == 503);
    assert(call(context, "GET", "/api/forwarders").status == 200);   // listed even without it
    smp_up = true;
    r = call(context, "POST", "/api/forwarders/delete", R"({"id":"PPSA99301"})");
    assert(r.status == 200 && json::parse(r.body)["state"] == "pending" && json::parse(r.body)["worker"] == true &&
           access((tile + "/remove.pending").c_str(), F_OK) == 0 && workers == 2);
    assert(call(context, "POST", "/api/forwarders/delete", R"({"id":"PPSA99170"})").status == 404);   // never MihawkOS
    std::vector<std::string> said;
    chmod((tile + "/eboot.bin").c_str(), 0644);   // a tile made with other modes
    chmod((tile + "/sce_sys/icon0.png").c_str(), 0666);
    pw_webui_forwarders_finish(context, [&said](const std::string &line) { said.push_back(line); });
    assert(said.front() == "forwarders: PPSA99301 modes mended" && stat((tile + "/eboot.bin").c_str(), &mode) == 0 &&
           (mode.st_mode & 0777) == 0755 && stat((tile + "/sce_sys/icon0.png").c_str(), &mode) == 0 &&
           (mode.st_mode & 0777) == 0644);
    assert(access(tile.c_str(), F_OK) == 0 && said.back() == "forwarders: 1 tile(s) wait for MihawkOS to close; the tiles' "
           "worker is sent" && workers == 3);
    // Ids in turn: PPSA99301, just removed, is not taken again; the next one is
    r = call(context, "POST", "/api/forwarders", R"({"game":"mihawk","name":"Again"})");
    assert(r.status == 200 && json::parse(r.body)["id"] == "PPSA99302" && get(data + "/tiles-next.txt") == "99303\n");

    // Saves: the user folders listed; a folder's ZIP planned, never the roots or a way out
    put(data + "/prefixes/mihawk/drive_c/users/Mihawk/Saved Games/slot1.sav", "SAVE1");
    assert(mkdir((data + "/prefixes/mihawk/drive_c/users/Mihawk/Saved Games/deep").c_str(), 0755) == 0);
    put(data + "/prefixes/mihawk/drive_c/users/Mihawk/Saved Games/deep/slot2.sav", "SAVE22");
    r = call(context, "GET", "/api/files", "", {{"path", "users/Mihawk"}});
    assert(r.status == 200 && json::parse(r.body)["dirs"][0] == "Saved Games");
    assert(call(context, "GET", "/api/files", "", {{"path", "Windows"}}).status == 404);
    r = call(context, "GET", "/api/prefixes");
    assert(r.status == 200 && json::parse(r.body)["prefixes"].size() == 1 && json::parse(r.body)["prefixes"][0] == "mihawk");
    r = call(context, "GET", "/api/file", "", {{"path", "users/Mihawk/Saved Games/slot1.sav"}});
    assert(r.status == 200 && r.download == "slot1.sav" && r.file.size() > 0);
    for (const char *bad : {"Windows/win.ini", "users/Mihawk/Saved Games", "users/../Games/x", "users/Mihawk/Saved Games/.x"})
        assert(call(context, "GET", "/api/file", "", {{"path", bad}}).status == 404);
    std::vector<PwWebuiZipFile> plan;
    assert(pw_webui_zip_plan(context, "mihawk", "users/Mihawk/Saved Games", &plan, &why) && plan.size() == 2);
    assert(plan[0].name == "users/Mihawk/Saved Games/deep/slot2.sav" && plan[0].size == 6 &&
           plan[1].name == "users/Mihawk/Saved Games/slot1.sav");
    for (const char *bad : {"users", "Games", "Windows", "users/../Games", "users/Mihawk/link"})
        assert(!pw_webui_zip_plan(context, "mihawk", bad, &plan, &why));

    assert(pw_webui_mask("a 255.255.255.255 b") == "a <address> b" && pw_webui_mask("1234.1.1.1") == "1234.1.1.1");
    std::string command = "rm -rf " + root;
    assert(system(command.c_str()) == 0);
    puts("webui: token, settings, library entries, picker, logs (masked), page files, every way out refused");
    return 0;
}
