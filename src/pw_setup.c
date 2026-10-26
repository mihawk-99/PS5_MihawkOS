/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The guided setup's steps, languages, text and record (pw_setup.h). */
#include "pw_setup.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

PwSetupStep pw_setup_start(const PwSetupFacts *facts)
{
    if (!facts->data) return PW_SETUP_NONE;
    if (facts->returning) return facts->prefix ? PW_SETUP_DONE : PW_SETUP_PREPARING;
    if (facts->asked || !facts->done) return PW_SETUP_WELCOME;
    if (!facts->runtime) return PW_SETUP_METHOD;
    if (!facts->prefix) return PW_SETUP_PREPARING;
    return PW_SETUP_NONE;
}

PwSetupStep pw_setup_next(PwSetupStep step, const PwSetupFacts *facts)
{
    switch (step) {
    case PW_SETUP_WELCOME: return PW_SETUP_LANGUAGE;
    case PW_SETUP_LANGUAGE: return PW_SETUP_METHOD;
    case PW_SETUP_METHOD: return facts->runtime ? pw_setup_next(PW_SETUP_INSTALLING, facts) : PW_SETUP_USB;
    case PW_SETUP_USB:
    case PW_SETUP_FTP: return PW_SETUP_INSTALLING;
    case PW_SETUP_INSTALLING: return facts->prefix ? PW_SETUP_DONE : PW_SETUP_PREPARING;
    case PW_SETUP_PREPARING: return PW_SETUP_DONE;
    default: return PW_SETUP_NONE;
    }
}

PwSetupStep pw_setup_back(PwSetupStep step)
{
    switch (step) {
    case PW_SETUP_LANGUAGE: return PW_SETUP_WELCOME;
    case PW_SETUP_METHOD: return PW_SETUP_LANGUAGE;
    case PW_SETUP_USB:
    case PW_SETUP_FTP: return PW_SETUP_METHOD;
    case PW_SETUP_PREPARING: return PW_SETUP_METHOD;
    default: return PW_SETUP_NONE;
    }
}

/* English (US). */
static const char *const text_en[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "Welcome to MihawkOS",
    [PW_TEXT_WELCOME_BODY] = "Let’s get your PS5 ready for Windows games. It takes about ten minutes.",
    [PW_TEXT_GET_STARTED] = "Get started",
    [PW_TEXT_SKIP_SETUP] = "Skip setup",
    [PW_TEXT_LANGUAGE_TITLE] = "Choose your language",
    [PW_TEXT_LANGUAGE_BODY] = "Your Windows games use it too, with its keyboard layout. The Mihawk OS desktop stays in English.",
    [PW_TEXT_METHOD_TITLE] = "Choose how to install",
    [PW_TEXT_METHOD_BODY] = "MihawkOS needs its Windows runtime, about 1.2 GB.",
    [PW_TEXT_METHOD_USB] = "USB drive",
    [PW_TEXT_METHOD_USB_BODY] = "Copy the files to a USB drive on any computer.",
    [PW_TEXT_METHOD_FTP] = "FTP",
    [PW_TEXT_METHOD_FTP_BODY] = "Copy the files to your PS5 over your network.",
    [PW_TEXT_METHOD_INTERNET] = "Internet",
    [PW_TEXT_METHOD_INTERNET_BODY] = "Download straight to your PS5.",
    [PW_TEXT_COMING_SOON] = "Coming soon",
    [PW_TEXT_METHOD_INSTALLED] = "The Windows runtime is already installed.",
    [PW_TEXT_CONTINUE] = "Continue",
    [PW_TEXT_QR_CAPTION] = "Scan for step-by-step instructions on your phone.",
    [PW_TEXT_USB_TITLE] = "Prepare a USB drive",
    [PW_TEXT_USB_STEP1] = "On a computer, format a USB drive as exFAT with an MBR partition table. This erases the drive.",
    [PW_TEXT_USB_STEP2] = "Download MihawkOS and copy its mihawkos folder to the top of the drive.",
    [PW_TEXT_USB_STEP3] = "Plug the drive into a USB port on your PS5.",
    [PW_TEXT_USB_STEP4] = "Choose Check drive.",
    [PW_TEXT_CHECK_DRIVE] = "Check drive",
    [PW_TEXT_FOUND] = "The drive holds {1} to install.",
    [PW_TEXT_INSTALL] = "Install",
    [PW_TEXT_RETRY] = "Try again",
    [PW_TEXT_FTP_TITLE] = "Copy the files over FTP",
    [PW_TEXT_FTP_STEP1] = "On your PS5, start an FTP server such as the ftpsrv payload (often port 2121).",
    [PW_TEXT_FTP_STEP2] = "On a computer, unzip MihawkOS’s usb-mihawkos file.",
    [PW_TEXT_FTP_STEP3] = "In an FTP client such as FileZilla, copy the files inside mihawkos to "
        "/data/ps5proton/install.",
    [PW_TEXT_FTP_STEP4] = "Choose Check files.",
    [PW_TEXT_CHECK_FILES] = "Check files",
    [PW_TEXT_FTP_FOUND] = "The install folder holds {1} to install.",
    [PW_TEXT_ERR_FTP_EMPTY] = "No runtime files in /data/ps5proton/install. Copy the files inside the mihawkos folder "
        "there, not the folder itself, then try again.",
    [PW_TEXT_ERR_FTP_PARTIAL] = "/data/ps5proton/install has {1} of {2} pieces. Copy the missing files, then try "
        "again.",
    [PW_TEXT_FTP_INSTALLING_BODY] = "The files are read from /data/ps5proton/install. This takes about a minute.",
    [PW_TEXT_ERR_FTP_READ] = "A file in /data/ps5proton/install could not be read. Copy the files there again, then "
        "try again. What is already installed is kept.",
    [PW_TEXT_ERR_FTP_DAMAGED] = "A file in /data/ps5proton/install is damaged or from another version. Copy the files "
        "inside the mihawkos folder again, then try again.",
    [PW_TEXT_ERR_NO_STICK] = "No USB drive found. Plug it in, wait a few seconds, then try again. If it is already in, "
        "format it as exFAT with MBR: the PS5 does not read GPT or NTFS drives.",
    [PW_TEXT_ERR_NO_FOLDER] = "The drive has no mihawkos folder at its top. Copy the folder to the drive, then try again.",
    [PW_TEXT_ERR_PARTIAL] = "The mihawkos folder is incomplete: it has {1} of {2} pieces. Copy the whole folder again, "
        "then try again.",
    [PW_TEXT_ERR_NO_SPACE] = "Your PS5 needs {1} free and has {2}. Delete something, then try again.",
    [PW_TEXT_ERR_READ] = "The USB drive could not be read. Check that it is still plugged in, then try again. What is "
        "already installed is kept.",
    [PW_TEXT_ERR_DAMAGED] = "A file on the drive is damaged or from another version. Copy the mihawkos folder again, "
        "then try again.",
    [PW_TEXT_ERR_FULL] = "Your PS5’s storage is full. Delete something, then try again. What is already installed "
        "is kept.",
    [PW_TEXT_ERR_WRITE] = "Your PS5’s storage refused a write. Try again; if it happens again, restart the PS5.",
    [PW_TEXT_ERR_CANCELLED] = "The install stopped. What is already installed is kept.",
    [PW_TEXT_ERR_BUILD] = "This copy of MihawkOS cannot install a Windows runtime. Reinstall MihawkOS.",
    [PW_TEXT_INSTALLING_TITLE] = "Installing the Windows runtime",
    [PW_TEXT_INSTALLING_BODY] = "Keep the drive plugged in. This takes about a minute.",
    [PW_TEXT_INSTALLING_CHECKING] = "Checking what is already installed…",
    [PW_TEXT_INSTALLING_STATS] = "{1} of {2} MB · {3} MB/s",
    [PW_TEXT_INSTALLED] = "Installed.",
    [PW_TEXT_STOP] = "Stop",
    [PW_TEXT_PREPARE_TITLE] = "Preparing Mihawk OS",
    [PW_TEXT_PREPARE_BODY] = "The first start builds your Windows drive. It takes a few minutes, and the screen may stay "
        "still for a while. It ends by itself and comes back here.",
    [PW_TEXT_PREPARE_NOTE] = "During this first start the controller does not work in games, so MihawkOS ends it as "
        "soon as the drive is ready.",
    [PW_TEXT_PREPARE_START] = "Start",
    [PW_TEXT_PREPARE_FAILED] = "The first start did not finish. Try again: it carries on where it stopped.",
    [PW_TEXT_BUILD_TITLE] = "Building your Windows drive",
    [PW_TEXT_BUILD_BODY] = "This takes about a minute. When the drive is ready, MihawkOS comes back to the setup by "
        "itself.",
    [PW_TEXT_BUILD_SETTINGS] = "Setting up Windows",
    [PW_TEXT_BUILD_DOTNET] = "Installing .NET support",
    [PW_TEXT_BUILD_FINISHING] = "Finishing up",
    [PW_TEXT_DONE_TITLE] = "You’re all set",
    [PW_TEXT_DONE_BODY] = "Mihawk OS is ready. You can run this setup again from Settings, in the library.",
    [PW_TEXT_ENTER] = "Enter Mihawk OS",
    [PW_TEXT_HINT_SELECT] = "Select",
    [PW_TEXT_HINT_BACK] = "Back",
    [PW_TEXT_HINT_MOVE] = "Move",
    [PW_TEXT_STEP_OF] = "Step {1} of {2}",
    [PW_TEXT_GIGABYTES] = "{1} GB",
    [PW_TEXT_DECIMAL] = ".",
    [PW_TEXT_STEAM_TITLE] = "Set up Steam",
    [PW_TEXT_STEAM_BODY] = "Steam is Valve’s app for your PC games. The first time, four things happen:",
    [PW_TEXT_STEAM_STEP_DOWNLOAD] = "Download",
    [PW_TEXT_STEAM_STEP_DOWNLOAD_BODY] = "Steam’s installer comes straight from Valve’s servers.",
    [PW_TEXT_STEAM_STEP_INSTALL] = "Install",
    [PW_TEXT_STEAM_STEP_INSTALL_BODY] = "MihawkOS installs it on your Windows drive in under a minute.",
    [PW_TEXT_STEAM_STEP_UPDATE] = "First start",
    [PW_TEXT_STEAM_STEP_UPDATE_BODY] = "Steam updates itself. This takes about 5 minutes.",
    [PW_TEXT_STEAM_STEP_SIGNIN] = "Sign in",
    [PW_TEXT_STEAM_STEP_SIGNIN_BODY] = "Scan a QR code with the Steam app on your phone.",
    [PW_TEXT_STEAM_NOTE] = "Needs an internet connection. About 10 minutes in all. MihawkOS is not affiliated with "
        "Valve.",
    [PW_TEXT_STEAM_START] = "Start",
    [PW_TEXT_STEAM_NOT_NOW] = "Not now",
    [PW_TEXT_STEAM_DOWNLOADING] = "Downloading Steam",
    [PW_TEXT_STEAM_DOWNLOADING_BODY] = "From Valve’s servers. MihawkOS installs Steam as soon as the download is "
        "done; the screen changes for a moment, which is normal.",
    [PW_TEXT_STEAM_MEGABYTES] = "{1} of about {2} MB",
    [PW_TEXT_STEAM_STARTING] = "Installed. Starting Steam…",
    [PW_TEXT_STEAM_FAIL_TITLE] = "Steam’s setup stopped",
    [PW_TEXT_STEAM_ERR_OFFLINE] = "Your PS5 is not connected to the internet. Check its network settings, then try "
        "again.",
    [PW_TEXT_STEAM_ERR_CLOCK] = "Your PS5’s date or time is wrong, so the secure download was refused. Set the date "
        "and time in the PS5’s settings, then try again.",
    [PW_TEXT_STEAM_ERR_DOWNLOAD] = "Steam’s installer could not be downloaded from Valve’s servers. Try again in a "
        "moment.",
    [PW_TEXT_STEAM_ERR_INSTALL] = "Steam’s installer did not finish. Try again: the download is kept.",
    [PW_TEXT_STEAM_ERR_STALLED] = "Steam’s update stopped moving for a long time, so Steam was closed. Try again: it "
        "carries on where it stopped.",
    [PW_TEXT_STEAM_ERR_CANCELLED] = "The sign-in was cancelled. Try again whenever you are ready.",
    [PW_TEXT_STEAM_ERR_TIMEOUT] = "No sign-in came within 30 minutes, so Steam was closed. Try again whenever you are "
        "ready.",
    [PW_TEXT_STEAM_ERR_GUARD] = "Steam asked for a Steam Guard code. Try again and use the QR code instead: in the "
        "Steam app on your phone, tap the shield, then scan the code.",
    [PW_TEXT_STEAM_ERR_ENDED] = "Steam closed before you signed in. Try again.",
    [PW_TEXT_STEAM_INSTALLING_TITLE] = "Installing Steam",
    [PW_TEXT_STEAM_INSTALLING_BODY] = "Valve’s installer is putting Steam on your Windows drive. This takes under a "
        "minute.",
    [PW_TEXT_STEAM_UPDATING_TITLE] = "Steam is updating itself",
    [PW_TEXT_STEAM_UPDATING_BODY] = "The first start downloads Steam’s latest version. It takes about 5 minutes, and "
        "the screen may stay still for a while: that is normal.",
    [PW_TEXT_STEAM_DOWNLOADED] = "{1} MB downloaded",
    [PW_TEXT_STEAM_ELAPSED] = "{1} elapsed",
    [PW_TEXT_STEAM_SLOW] = "This is taking longer than usual. You can keep waiting, or try again.",
    [PW_TEXT_STEAM_SCAN_TITLE] = "Get your phone ready",
    [PW_TEXT_STEAM_SCAN_BODY] = "Steam is opening its sign-in page. Open the Steam app on your phone, tap the shield, "
        "then scan the QR code on the screen.",
    [PW_TEXT_STEAM_CANCEL_NOTE] = "To cancel, hold OPTIONS for 2 seconds.",
    [PW_TEXT_STEAM_SIGNED_IN] = "You’re signed in",
    [PW_TEXT_STEAM_SIGNED_IN_AS] = "You’re signed in as {1}",
    [PW_TEXT_STEAM_REOPEN_BODY] = "Steam closes now and reopens in Big Picture: made for the controller, with "
        "settings tuned for the PS5. From now on, Steam opens straight into Big Picture.",
    [PW_TEXT_STEAM_DONE_BODY] = "From now on, Steam opens straight into Big Picture: made for the controller, with "
        "settings tuned for the PS5. You won’t need to sign in again.",
    [PW_TEXT_STEAM_OPENING_IN] = "Opening in {1} s",
};

/* Svenska (Sverige). */
static const char *const text_sv[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "Välkommen till MihawkOS",
    [PW_TEXT_WELCOME_BODY] = "Vi gör din PS5 redo för Windows-spel. Det tar ungefär tio minuter.",
    [PW_TEXT_GET_STARTED] = "Kom i gång",
    [PW_TEXT_SKIP_SETUP] = "Hoppa över konfigurationen",
    [PW_TEXT_LANGUAGE_TITLE] = "Välj språk",
    [PW_TEXT_LANGUAGE_BODY] = "Dina Windows-spel använder också språket och dess tangentbordslayout. Mihawk OS-skrivbordet är på engelska.",
    [PW_TEXT_METHOD_TITLE] = "Välj hur du vill installera",
    [PW_TEXT_METHOD_BODY] = "MihawkOS behöver sin Windows-miljö, ungefär 1,2 GB.",
    [PW_TEXT_METHOD_USB] = "USB-enhet",
    [PW_TEXT_METHOD_USB_BODY] = "Kopiera filerna till en USB-enhet på valfri dator.",
    [PW_TEXT_METHOD_INTERNET] = "Internet",
    [PW_TEXT_METHOD_INTERNET_BODY] = "Hämta filerna direkt till PS5:an.",
    [PW_TEXT_COMING_SOON] = "Kommer snart",
    [PW_TEXT_METHOD_INSTALLED] = "Windows-miljön är redan installerad.",
    [PW_TEXT_CONTINUE] = "Fortsätt",
    [PW_TEXT_QR_CAPTION] = "Skanna för stegvisa instruktioner på telefonen.",
    [PW_TEXT_USB_TITLE] = "Förbered en USB-enhet",
    [PW_TEXT_USB_STEP1] = "Formatera en USB-enhet på en dator som exFAT med MBR-partitionstabell. Då raderas enheten.",
    [PW_TEXT_USB_STEP2] = "Hämta MihawkOS och kopiera mappen mihawkos till enhetens rot.",
    [PW_TEXT_USB_STEP3] = "Anslut enheten till en USB-port på PS5:an.",
    [PW_TEXT_USB_STEP4] = "Välj Kontrollera enhet.",
    [PW_TEXT_CHECK_DRIVE] = "Kontrollera enhet",
    [PW_TEXT_FOUND] = "Enheten innehåller {1} som kan installeras.",
    [PW_TEXT_INSTALL] = "Installera",
    [PW_TEXT_RETRY] = "Försök igen",
    [PW_TEXT_ERR_NO_STICK] = "Ingen USB-enhet hittades. Anslut den, vänta några sekunder och försök sedan igen. Om den redan är ansluten, formatera den som exFAT med MBR: PS5:an kan inte läsa GPT- eller NTFS-enheter.",
    [PW_TEXT_ERR_NO_FOLDER] = "Enheten saknar mappen mihawkos i roten. Kopiera mappen till enheten och försök sedan igen.",
    [PW_TEXT_ERR_PARTIAL] = "Mappen mihawkos är ofullständig: den innehåller {1} av {2} delar. Kopiera hela mappen igen och försök sedan igen.",
    [PW_TEXT_ERR_NO_SPACE] = "PS5:an behöver {1} ledigt utrymme och har {2}. Ta bort något och försök sedan igen.",
    [PW_TEXT_ERR_READ] = "Det gick inte att läsa USB-enheten. Kontrollera att den fortfarande är ansluten och försök igen. Det som redan är installerat behålls.",
    [PW_TEXT_ERR_DAMAGED] = "En fil på enheten är skadad eller kommer från en annan version. Kopiera mappen mihawkos igen och försök sedan igen.",
    [PW_TEXT_ERR_FULL] = "PS5:ans lagring är full. Ta bort något och försök sedan igen. Det som redan är installerat behålls.",
    [PW_TEXT_ERR_WRITE] = "PS5:ans lagring kunde inte skriva data. Försök igen. Starta om PS5:an om det händer igen.",
    [PW_TEXT_ERR_CANCELLED] = "Installationen stoppades. Det som redan är installerat behålls.",
    [PW_TEXT_ERR_BUILD] = "Den här kopian av MihawkOS kan inte installera en Windows-miljö. Installera om MihawkOS.",
    [PW_TEXT_INSTALLING_TITLE] = "Installerar Windows-miljön",
    [PW_TEXT_INSTALLING_BODY] = "Låt enheten vara ansluten. Det tar ungefär en minut.",
    [PW_TEXT_INSTALLING_CHECKING] = "Kontrollerar det som redan är installerat…",
    [PW_TEXT_INSTALLING_STATS] = "{1} av {2} MB · {3} MB/s",
    [PW_TEXT_INSTALLED] = "Installerat.",
    [PW_TEXT_STOP] = "Stoppa",
    [PW_TEXT_PREPARE_TITLE] = "Förbereder Mihawk OS",
    [PW_TEXT_PREPARE_BODY] = "Första starten bygger din Windows-enhet. Det tar några minuter och skärmen kan stå still en stund. Programmet avslutas automatiskt och återgår hit.",
    [PW_TEXT_PREPARE_NOTE] = "Handkontrollen fungerar inte i spel under den första starten, så MihawkOS avslutar den när enheten är klar.",
    [PW_TEXT_PREPARE_START] = "Starta",
    [PW_TEXT_PREPARE_FAILED] = "Första starten slutfördes inte. Försök igen; den fortsätter där den slutade.",
    [PW_TEXT_BUILD_TITLE] = "Bygger din Windows-enhet",
    [PW_TEXT_BUILD_BODY] = "Det tar ungefär en minut. När enheten är klar återgår MihawkOS automatiskt till konfigurationen.",
    [PW_TEXT_BUILD_SETTINGS] = "Konfigurerar Windows",
    [PW_TEXT_BUILD_DOTNET] = "Installerar .NET-stöd",
    [PW_TEXT_BUILD_FINISHING] = "Slutför",
    [PW_TEXT_DONE_TITLE] = "Allt är klart",
    [PW_TEXT_DONE_BODY] = "Mihawk OS är redo. Du kan köra konfigurationen igen från Inställningar i biblioteket.",
    [PW_TEXT_ENTER] = "Öppna Mihawk OS",
    [PW_TEXT_HINT_SELECT] = "Välj",
    [PW_TEXT_HINT_BACK] = "Tillbaka",
    [PW_TEXT_HINT_MOVE] = "Flytta",
    [PW_TEXT_STEP_OF] = "Steg {1} av {2}",
    [PW_TEXT_GIGABYTES] = "{1} GB",
    [PW_TEXT_DECIMAL] = ",",
};

static const char *const text_fr[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "Bienvenue sur MihawkOS",
    [PW_TEXT_WELCOME_BODY] = "Préparons votre PS5 pour les jeux Windows. Comptez une dizaine de minutes.",
    [PW_TEXT_GET_STARTED] = "Commencer",
    [PW_TEXT_SKIP_SETUP] = "Passer la configuration",
    [PW_TEXT_LANGUAGE_TITLE] = "Choisissez votre langue",
    [PW_TEXT_LANGUAGE_BODY] = "Vos jeux Windows l’utilisent aussi, avec sa disposition de clavier. Le bureau de "
        "Mihawk OS reste en anglais.",
    [PW_TEXT_METHOD_TITLE] = "Choisissez comment installer",
    [PW_TEXT_METHOD_BODY] = "MihawkOS a besoin de son environnement Windows, environ 1,2 Go.",
    [PW_TEXT_METHOD_USB] = "Clé USB",
    [PW_TEXT_METHOD_USB_BODY] = "Copiez les fichiers sur une clé USB depuis n’importe quel ordinateur.",
    [PW_TEXT_METHOD_FTP] = "FTP",
    [PW_TEXT_METHOD_FTP_BODY] = "Copiez les fichiers sur votre PS5 par votre réseau.",
    [PW_TEXT_METHOD_INTERNET] = "Internet",
    [PW_TEXT_METHOD_INTERNET_BODY] = "Téléchargez directement sur votre PS5.",
    [PW_TEXT_COMING_SOON] = "Bientôt disponible",
    [PW_TEXT_METHOD_INSTALLED] = "L’environnement Windows est déjà installé.",
    [PW_TEXT_CONTINUE] = "Continuer",
    [PW_TEXT_QR_CAPTION] = "Scannez pour suivre les instructions pas à pas sur votre téléphone.",
    [PW_TEXT_USB_TITLE] = "Préparez une clé USB",
    [PW_TEXT_USB_STEP1] = "Sur un ordinateur, formatez une clé USB en exFAT avec une table de partition MBR. "
        "Cela efface la clé.",
    [PW_TEXT_USB_STEP2] = "Téléchargez MihawkOS et copiez son dossier mihawkos à la racine de la clé.",
    [PW_TEXT_USB_STEP3] = "Branchez la clé sur un port USB de votre PS5.",
    [PW_TEXT_USB_STEP4] = "Choisissez Vérifier la clé.",
    [PW_TEXT_CHECK_DRIVE] = "Vérifier la clé",
    [PW_TEXT_FOUND] = "La clé contient {1} à installer.",
    [PW_TEXT_INSTALL] = "Installer",
    [PW_TEXT_RETRY] = "Réessayer",
    [PW_TEXT_FTP_TITLE] = "Copiez les fichiers par FTP",
    [PW_TEXT_FTP_STEP1] = "Sur votre PS5, lancez un serveur FTP comme le payload ftpsrv (souvent port 2121).",
    [PW_TEXT_FTP_STEP2] = "Sur un ordinateur, décompressez le fichier usb-mihawkos de MihawkOS.",
    [PW_TEXT_FTP_STEP3] = "Avec un client FTP comme FileZilla, copiez les fichiers du dossier mihawkos dans "
        "/data/ps5proton/install.",
    [PW_TEXT_FTP_STEP4] = "Choisissez Vérifier les fichiers.",
    [PW_TEXT_CHECK_FILES] = "Vérifier les fichiers",
    [PW_TEXT_FTP_FOUND] = "Le dossier d’installation contient {1} à installer.",
    [PW_TEXT_ERR_FTP_EMPTY] = "Aucun fichier de l’environnement dans /data/ps5proton/install. Copiez-y les fichiers "
        "du dossier mihawkos, pas le dossier lui-même, puis réessayez.",
    [PW_TEXT_ERR_FTP_PARTIAL] = "/data/ps5proton/install contient {1} morceaux sur {2}. Copiez les fichiers "
        "manquants, puis réessayez.",
    [PW_TEXT_FTP_INSTALLING_BODY] = "Les fichiers sont lus dans /data/ps5proton/install. Cela prend environ une "
        "minute.",
    [PW_TEXT_ERR_FTP_READ] = "Impossible de lire un fichier de /data/ps5proton/install. Copiez de nouveau les "
        "fichiers, puis réessayez. Ce qui est déjà installé est conservé.",
    [PW_TEXT_ERR_FTP_DAMAGED] = "Un fichier de /data/ps5proton/install est endommagé ou d’une autre version. Copiez "
        "de nouveau les fichiers du dossier mihawkos, puis réessayez.",
    [PW_TEXT_ERR_NO_STICK] = "Aucune clé USB trouvée. Branchez-la, attendez quelques secondes, puis "
        "réessayez. Si elle est déjà branchée, formatez-la en exFAT avec MBR : la PS5 ne lit pas "
        "les clés GPT ou NTFS.",
    [PW_TEXT_ERR_NO_FOLDER] = "La clé n’a pas de dossier mihawkos à sa racine. Copiez le dossier sur la "
        "clé, puis réessayez.",
    [PW_TEXT_ERR_PARTIAL] = "Le dossier mihawkos est incomplet : il contient {1} morceaux sur {2}. Copiez de nouveau le "
        "dossier entier, puis réessayez.",
    [PW_TEXT_ERR_NO_SPACE] = "Votre PS5 a besoin de {1} libres et n’en a que {2}. Supprimez quelque chose, puis "
        "réessayez.",
    [PW_TEXT_ERR_READ] = "Impossible de lire la clé USB. Vérifiez qu’elle est toujours branchée, "
        "puis réessayez. Ce qui est déjà installé est conservé.",
    [PW_TEXT_ERR_DAMAGED] = "Un fichier de la clé est endommagé ou d’une autre version. Copiez de nouveau "
        "le dossier mihawkos, puis réessayez.",
    [PW_TEXT_ERR_FULL] = "Le stockage de votre PS5 est plein. Supprimez quelque chose, puis réessayez. Ce qui est "
        "déjà installé est conservé.",
    [PW_TEXT_ERR_WRITE] = "Le stockage de votre PS5 a refusé une écriture. Réessayez ; si cela se "
        "reproduit, redémarrez la PS5.",
    [PW_TEXT_ERR_CANCELLED] = "Installation arrêtée. Ce qui est déjà installé est "
        "conservé.",
    [PW_TEXT_ERR_BUILD] = "Cette copie de MihawkOS ne peut pas installer d’environnement Windows. Réinstallez "
        "MihawkOS.",
    [PW_TEXT_INSTALLING_TITLE] = "Installation de l’environnement Windows",
    [PW_TEXT_INSTALLING_BODY] = "Laissez la clé branchée. Cela prend environ une minute.",
    [PW_TEXT_INSTALLING_CHECKING] = "Vérification de ce qui est déjà installé…",
    [PW_TEXT_INSTALLING_STATS] = "{1} sur {2} Mo · {3} Mo/s",
    [PW_TEXT_INSTALLED] = "Installé.",
    [PW_TEXT_STOP] = "Arrêter",
    [PW_TEXT_PREPARE_TITLE] = "Préparation de Mihawk OS",
    [PW_TEXT_PREPARE_BODY] = "Le premier démarrage crée votre disque Windows. Cela prend quelques minutes, et "
        "l’écran peut rester immobile un moment. Il se termine tout seul et revient ici.",
    [PW_TEXT_PREPARE_NOTE] = "Pendant ce premier démarrage, la manette ne fonctionne pas dans les jeux : MihawkOS "
        "le termine donc dès que le disque est prêt.",
    [PW_TEXT_PREPARE_START] = "Démarrer",
    [PW_TEXT_PREPARE_FAILED] = "Le premier démarrage ne s’est pas terminé. Réessayez : il reprend "
        "là où il s’est arrêté.",
    [PW_TEXT_BUILD_TITLE] = "Création de votre disque Windows",
    [PW_TEXT_BUILD_BODY] = "Cela prend environ une minute. Une fois le disque prêt, MihawkOS revient tout seul à la "
        "configuration.",
    [PW_TEXT_BUILD_SETTINGS] = "Configuration de Windows",
    [PW_TEXT_BUILD_DOTNET] = "Installation de la prise en charge de .NET",
    [PW_TEXT_BUILD_FINISHING] = "Finalisation",
    [PW_TEXT_DONE_TITLE] = "Tout est prêt",
    [PW_TEXT_DONE_BODY] = "Mihawk OS est prêt. Vous pouvez relancer cette configuration depuis Settings, dans la "
        "bibliothèque.",
    [PW_TEXT_ENTER] = "Entrer dans Mihawk OS",
    [PW_TEXT_HINT_SELECT] = "Choisir",
    [PW_TEXT_HINT_BACK] = "Retour",
    [PW_TEXT_HINT_MOVE] = "Déplacer",
    [PW_TEXT_STEP_OF] = "Étape {1} sur {2}",
    [PW_TEXT_GIGABYTES] = "{1} Go",
    [PW_TEXT_DECIMAL] = ",",
    [PW_TEXT_STEAM_TITLE] = "Configurer Steam",
    [PW_TEXT_STEAM_BODY] = "Steam est l’application de Valve pour vos jeux PC. La première fois, quatre étapes "
        "s’enchaînent :",
    [PW_TEXT_STEAM_STEP_DOWNLOAD] = "Téléchargement",
    [PW_TEXT_STEAM_STEP_DOWNLOAD_BODY] = "Le programme d’installation de Steam vient directement des serveurs de "
        "Valve.",
    [PW_TEXT_STEAM_STEP_INSTALL] = "Installation",
    [PW_TEXT_STEAM_STEP_INSTALL_BODY] = "MihawkOS l’installe sur votre disque Windows en moins d’une minute.",
    [PW_TEXT_STEAM_STEP_UPDATE] = "Premier démarrage",
    [PW_TEXT_STEAM_STEP_UPDATE_BODY] = "Steam se met à jour tout seul. Cela prend environ 5 minutes.",
    [PW_TEXT_STEAM_STEP_SIGNIN] = "Connexion",
    [PW_TEXT_STEAM_STEP_SIGNIN_BODY] = "Scannez un code QR avec l’application Steam de votre téléphone.",
    [PW_TEXT_STEAM_NOTE] = "Une connexion Internet est nécessaire. Environ 10 minutes en tout. MihawkOS n’est pas "
        "affilié à Valve.",
    [PW_TEXT_STEAM_START] = "Commencer",
    [PW_TEXT_STEAM_NOT_NOW] = "Plus tard",
    [PW_TEXT_STEAM_DOWNLOADING] = "Téléchargement de Steam",
    [PW_TEXT_STEAM_DOWNLOADING_BODY] = "Depuis les serveurs de Valve. MihawkOS installe Steam dès la fin du "
        "téléchargement ; l’écran change un instant, c’est normal.",
    [PW_TEXT_STEAM_MEGABYTES] = "{1} Mo sur environ {2} Mo",
    [PW_TEXT_STEAM_STARTING] = "Installé. Démarrage de Steam…",
    [PW_TEXT_STEAM_FAIL_TITLE] = "La configuration de Steam s’est arrêtée",
    [PW_TEXT_STEAM_ERR_OFFLINE] = "Votre PS5 n’est pas connectée à Internet. Vérifiez ses paramètres réseau, puis "
        "réessayez.",
    [PW_TEXT_STEAM_ERR_CLOCK] = "La date ou l’heure de votre PS5 est incorrecte, le téléchargement sécurisé a donc "
        "été refusé. Réglez la date et l’heure dans les paramètres de la PS5, puis réessayez.",
    [PW_TEXT_STEAM_ERR_DOWNLOAD] = "Impossible de télécharger le programme d’installation de Steam depuis les "
        "serveurs de Valve. Réessayez dans un instant.",
    [PW_TEXT_STEAM_ERR_INSTALL] = "Le programme d’installation de Steam ne s’est pas terminé. Réessayez : le "
        "téléchargement est conservé.",
    [PW_TEXT_STEAM_ERR_STALLED] = "La mise à jour de Steam n’a plus avancé pendant longtemps, Steam a donc été fermé. "
        "Réessayez : elle reprend là où elle s’est arrêtée.",
    [PW_TEXT_STEAM_ERR_CANCELLED] = "La connexion a été annulée. Réessayez quand vous le souhaitez.",
    [PW_TEXT_STEAM_ERR_TIMEOUT] = "Aucune connexion en 30 minutes, Steam a donc été fermé. Réessayez quand vous le "
        "souhaitez.",
    [PW_TEXT_STEAM_ERR_GUARD] = "Steam a demandé un code Steam Guard. Réessayez en utilisant plutôt le code QR : dans "
        "l’application Steam de votre téléphone, touchez le bouclier, puis scannez le code.",
    [PW_TEXT_STEAM_ERR_ENDED] = "Steam s’est fermé avant la connexion. Réessayez.",
    [PW_TEXT_STEAM_INSTALLING_TITLE] = "Installation de Steam",
    [PW_TEXT_STEAM_INSTALLING_BODY] = "Le programme d’installation de Valve installe Steam sur votre disque Windows. "
        "Cela prend moins d’une minute.",
    [PW_TEXT_STEAM_UPDATING_TITLE] = "Steam se met à jour",
    [PW_TEXT_STEAM_UPDATING_BODY] = "Le premier démarrage télécharge la dernière version de Steam. Cela prend environ "
        "5 minutes, et l’écran peut rester immobile un moment : c’est normal.",
    [PW_TEXT_STEAM_DOWNLOADED] = "{1} Mo téléchargés",
    [PW_TEXT_STEAM_ELAPSED] = "Temps écoulé : {1}",
    [PW_TEXT_STEAM_SLOW] = "Cela prend plus de temps que d’habitude. Vous pouvez continuer à attendre ou réessayer.",
    [PW_TEXT_STEAM_SCAN_TITLE] = "Préparez votre téléphone",
    [PW_TEXT_STEAM_SCAN_BODY] = "Steam ouvre sa page de connexion. Ouvrez l’application Steam sur votre téléphone, "
        "touchez le bouclier, puis scannez le code QR affiché à l’écran.",
    [PW_TEXT_STEAM_CANCEL_NOTE] = "Pour annuler, maintenez OPTIONS pendant 2 secondes.",
    [PW_TEXT_STEAM_SIGNED_IN] = "Connexion réussie",
    [PW_TEXT_STEAM_SIGNED_IN_AS] = "Connexion réussie : {1}",
    [PW_TEXT_STEAM_REOPEN_BODY] = "Steam se ferme maintenant et se rouvre en mode Big Picture : conçu pour la "
        "manette, avec des réglages adaptés à la PS5. Désormais, Steam s’ouvre directement en Big Picture.",
    [PW_TEXT_STEAM_DONE_BODY] = "Désormais, Steam s’ouvre directement en mode Big Picture : conçu pour la manette, "
        "avec des réglages adaptés à la PS5. Vous n’aurez plus à vous connecter.",
    [PW_TEXT_STEAM_OPENING_IN] = "Ouverture dans {1} s",
};

static const char *const text_es[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "Bienvenido a MihawkOS",
    [PW_TEXT_WELCOME_BODY] = "Vamos a preparar tu PS5 para los juegos de Windows. Tardarás unos diez minutos.",
    [PW_TEXT_GET_STARTED] = "Empezar",
    [PW_TEXT_SKIP_SETUP] = "Omitir la configuración",
    [PW_TEXT_LANGUAGE_TITLE] = "Elige tu idioma",
    [PW_TEXT_LANGUAGE_BODY] = "Tus juegos de Windows también lo usarán, con su distribución de teclado. El escritorio de "
        "Mihawk OS sigue en inglés.",
    [PW_TEXT_METHOD_TITLE] = "Elige cómo instalar",
    [PW_TEXT_METHOD_BODY] = "MihawkOS necesita su entorno de Windows, de unos 1,2 GB.",
    [PW_TEXT_METHOD_USB] = "Unidad USB",
    [PW_TEXT_METHOD_USB_BODY] = "Copia los archivos en una unidad USB desde cualquier ordenador.",
    [PW_TEXT_METHOD_FTP] = "FTP",
    [PW_TEXT_METHOD_FTP_BODY] = "Copia los archivos a tu PS5 a través de tu red.",
    [PW_TEXT_METHOD_INTERNET] = "Internet",
    [PW_TEXT_METHOD_INTERNET_BODY] = "Descárgalo directamente en tu PS5.",
    [PW_TEXT_COMING_SOON] = "Próximamente",
    [PW_TEXT_METHOD_INSTALLED] = "El entorno de Windows ya está instalado.",
    [PW_TEXT_CONTINUE] = "Continuar",
    [PW_TEXT_QR_CAPTION] = "Escanea el código para ver las instrucciones paso a paso en tu móvil.",
    [PW_TEXT_USB_TITLE] = "Prepara una unidad USB",
    [PW_TEXT_USB_STEP1] = "En un ordenador, formatea una unidad USB como exFAT con una tabla de particiones MBR. Esto borra "
        "la unidad.",
    [PW_TEXT_USB_STEP2] = "Descarga MihawkOS y copia su carpeta mihawkos en la raíz de la unidad.",
    [PW_TEXT_USB_STEP3] = "Conecta la unidad a un puerto USB de tu PS5.",
    [PW_TEXT_USB_STEP4] = "Elige Comprobar unidad.",
    [PW_TEXT_CHECK_DRIVE] = "Comprobar unidad",
    [PW_TEXT_FOUND] = "La unidad contiene {1} para instalar.",
    [PW_TEXT_INSTALL] = "Instalar",
    [PW_TEXT_RETRY] = "Reintentar",
    [PW_TEXT_FTP_TITLE] = "Copia los archivos por FTP",
    [PW_TEXT_FTP_STEP1] = "En tu PS5, inicia un servidor FTP como el payload ftpsrv (a menudo puerto 2121).",
    [PW_TEXT_FTP_STEP2] = "En un ordenador, descomprime el archivo usb-mihawkos de MihawkOS.",
    [PW_TEXT_FTP_STEP3] = "Con un cliente FTP como FileZilla, copia los archivos de la carpeta mihawkos en "
        "/data/ps5proton/install.",
    [PW_TEXT_FTP_STEP4] = "Elige Comprobar archivos.",
    [PW_TEXT_CHECK_FILES] = "Comprobar archivos",
    [PW_TEXT_FTP_FOUND] = "La carpeta de instalación contiene {1} para instalar.",
    [PW_TEXT_ERR_FTP_EMPTY] = "No hay archivos del entorno en /data/ps5proton/install. Copia allí los archivos de la "
        "carpeta mihawkos, no la carpeta, y vuelve a intentarlo.",
    [PW_TEXT_ERR_FTP_PARTIAL] = "/data/ps5proton/install tiene {1} de {2} partes. Copia los archivos que faltan y "
        "vuelve a intentarlo.",
    [PW_TEXT_FTP_INSTALLING_BODY] = "Los archivos se leen de /data/ps5proton/install. Tarda alrededor de un minuto.",
    [PW_TEXT_ERR_FTP_READ] = "No se puede leer un archivo de /data/ps5proton/install. Copia de nuevo los archivos y "
        "vuelve a intentarlo. Lo que ya está instalado se conserva.",
    [PW_TEXT_ERR_FTP_DAMAGED] = "Un archivo de /data/ps5proton/install está dañado o es de otra versión. Copia de "
        "nuevo los archivos de la carpeta mihawkos y vuelve a intentarlo.",
    [PW_TEXT_ERR_NO_STICK] = "No se encuentra ninguna unidad USB. Conéctala, espera unos segundos y vuelve a intentarlo. "
        "Si ya está conectada, formatéala como exFAT con MBR: la PS5 no lee unidades GPT ni NTFS.",
    [PW_TEXT_ERR_NO_FOLDER] = "La unidad no tiene una carpeta mihawkos en su raíz. Copia la carpeta en la unidad y vuelve "
        "a intentarlo.",
    [PW_TEXT_ERR_PARTIAL] = "La carpeta mihawkos está incompleta: tiene {1} de {2} partes. Copia de nuevo la carpeta entera "
        "y vuelve a intentarlo.",
    [PW_TEXT_ERR_NO_SPACE] = "Tu PS5 necesita {1} libres y tiene {2}. Borra algo y vuelve a intentarlo.",
    [PW_TEXT_ERR_READ] = "No se puede leer la unidad USB. Comprueba que sigue conectada y vuelve a intentarlo. Lo que ya "
        "está instalado se conserva.",
    [PW_TEXT_ERR_DAMAGED] = "Un archivo de la unidad está dañado o es de otra versión. Copia de nuevo la carpeta mihawkos "
        "y vuelve a intentarlo.",
    [PW_TEXT_ERR_FULL] = "El almacenamiento de tu PS5 está lleno. Borra algo y vuelve a intentarlo. Lo que ya está instalado "
        "se conserva.",
    [PW_TEXT_ERR_WRITE] = "El almacenamiento de tu PS5 ha rechazado una escritura. Vuelve a intentarlo; si pasa de nuevo, "
        "reinicia la PS5.",
    [PW_TEXT_ERR_CANCELLED] = "La instalación se ha detenido. Lo que ya está instalado se conserva.",
    [PW_TEXT_ERR_BUILD] = "Esta copia de MihawkOS no puede instalar un entorno de Windows. Vuelve a instalar MihawkOS.",
    [PW_TEXT_INSTALLING_TITLE] = "Instalando el entorno de Windows",
    [PW_TEXT_INSTALLING_BODY] = "Deja la unidad conectada. Tarda alrededor de un minuto.",
    [PW_TEXT_INSTALLING_CHECKING] = "Comprobando lo que ya está instalado…",
    [PW_TEXT_INSTALLING_STATS] = "{1} de {2} MB · {3} MB/s",
    [PW_TEXT_INSTALLED] = "Instalado.",
    [PW_TEXT_STOP] = "Detener",
    [PW_TEXT_PREPARE_TITLE] = "Preparando Mihawk OS",
    [PW_TEXT_PREPARE_BODY] = "El primer inicio crea tu disco de Windows. Tarda unos minutos y la pantalla puede quedarse "
        "quieta un rato. Termina solo y vuelve aquí.",
    [PW_TEXT_PREPARE_NOTE] = "Durante este primer inicio el mando no funciona en los juegos, así que MihawkOS lo termina "
        "en cuanto el disco está listo.",
    [PW_TEXT_PREPARE_START] = "Iniciar",
    [PW_TEXT_PREPARE_FAILED] = "El primer inicio no ha terminado. Vuelve a intentarlo: continúa donde se quedó.",
    [PW_TEXT_BUILD_TITLE] = "Creando tu disco de Windows",
    [PW_TEXT_BUILD_BODY] = "Tarda alrededor de un minuto. Cuando el disco esté listo, MihawkOS vuelve solo a la "
        "configuración.",
    [PW_TEXT_BUILD_SETTINGS] = "Configurando Windows",
    [PW_TEXT_BUILD_DOTNET] = "Instalando la compatibilidad con .NET",
    [PW_TEXT_BUILD_FINISHING] = "Terminando",
    [PW_TEXT_DONE_TITLE] = "Todo listo",
    [PW_TEXT_DONE_BODY] = "Mihawk OS está listo. Puedes repetir esta configuración desde Settings, en la biblioteca.",
    [PW_TEXT_ENTER] = "Entrar en Mihawk OS",
    [PW_TEXT_HINT_SELECT] = "Elegir",
    [PW_TEXT_HINT_BACK] = "Atrás",
    [PW_TEXT_HINT_MOVE] = "Mover",
    [PW_TEXT_STEP_OF] = "Paso {1} de {2}",
    [PW_TEXT_GIGABYTES] = "{1} GB",
    [PW_TEXT_DECIMAL] = ",",
    [PW_TEXT_STEAM_TITLE] = "Configurar Steam",
    [PW_TEXT_STEAM_BODY] = "Steam es la aplicación de Valve para tus juegos de PC. La primera vez pasan cuatro cosas:",
    [PW_TEXT_STEAM_STEP_DOWNLOAD] = "Descarga",
    [PW_TEXT_STEAM_STEP_DOWNLOAD_BODY] = "El instalador de Steam llega directamente de los servidores de Valve.",
    [PW_TEXT_STEAM_STEP_INSTALL] = "Instalación",
    [PW_TEXT_STEAM_STEP_INSTALL_BODY] = "MihawkOS lo instala en tu disco de Windows en menos de un minuto.",
    [PW_TEXT_STEAM_STEP_UPDATE] = "Primer inicio",
    [PW_TEXT_STEAM_STEP_UPDATE_BODY] = "Steam se actualiza solo. Tarda unos 5 minutos.",
    [PW_TEXT_STEAM_STEP_SIGNIN] = "Inicio de sesión",
    [PW_TEXT_STEAM_STEP_SIGNIN_BODY] = "Escanea un código QR con la aplicación de Steam de tu móvil.",
    [PW_TEXT_STEAM_NOTE] = "Necesitas conexión a internet. Unos 10 minutos en total. MihawkOS no está afiliado a "
        "Valve.",
    [PW_TEXT_STEAM_START] = "Empezar",
    [PW_TEXT_STEAM_NOT_NOW] = "Ahora no",
    [PW_TEXT_STEAM_DOWNLOADING] = "Descargando Steam",
    [PW_TEXT_STEAM_DOWNLOADING_BODY] = "Desde los servidores de Valve. MihawkOS instala Steam en cuanto termina la "
        "descarga; la pantalla cambia un momento, es normal.",
    [PW_TEXT_STEAM_MEGABYTES] = "{1} de unos {2} MB",
    [PW_TEXT_STEAM_STARTING] = "Instalado. Iniciando Steam…",
    [PW_TEXT_STEAM_FAIL_TITLE] = "La configuración de Steam se ha detenido",
    [PW_TEXT_STEAM_ERR_OFFLINE] = "Tu PS5 no está conectada a internet. Revisa su configuración de red y vuelve a "
        "intentarlo.",
    [PW_TEXT_STEAM_ERR_CLOCK] = "La fecha o la hora de tu PS5 no son correctas, así que se ha rechazado la descarga "
        "segura. Ajusta la fecha y la hora en la configuración de la PS5 y vuelve a intentarlo.",
    [PW_TEXT_STEAM_ERR_DOWNLOAD] = "No se ha podido descargar el instalador de Steam de los servidores de Valve. "
        "Vuelve a intentarlo en un momento.",
    [PW_TEXT_STEAM_ERR_INSTALL] = "El instalador de Steam no ha terminado. Vuelve a intentarlo: la descarga se "
        "conserva.",
    [PW_TEXT_STEAM_ERR_STALLED] = "La actualización de Steam no ha avanzado durante mucho tiempo, así que se ha "
        "cerrado Steam. Vuelve a intentarlo: continúa donde se quedó.",
    [PW_TEXT_STEAM_ERR_CANCELLED] = "Se ha cancelado el inicio de sesión. Vuelve a intentarlo cuando quieras.",
    [PW_TEXT_STEAM_ERR_TIMEOUT] = "No se ha iniciado sesión en 30 minutos, así que se ha cerrado Steam. Vuelve a "
        "intentarlo cuando quieras.",
    [PW_TEXT_STEAM_ERR_GUARD] = "Steam ha pedido un código de Steam Guard. Vuelve a intentarlo y usa el código QR: en "
        "la aplicación de Steam de tu móvil, toca el escudo y escanea el código.",
    [PW_TEXT_STEAM_ERR_ENDED] = "Steam se ha cerrado antes de que iniciaras sesión. Vuelve a intentarlo.",
    [PW_TEXT_STEAM_INSTALLING_TITLE] = "Instalando Steam",
    [PW_TEXT_STEAM_INSTALLING_BODY] = "El instalador de Valve está poniendo Steam en tu disco de Windows. Tarda menos "
        "de un minuto.",
    [PW_TEXT_STEAM_UPDATING_TITLE] = "Steam se está actualizando",
    [PW_TEXT_STEAM_UPDATING_BODY] = "El primer inicio descarga la última versión de Steam. Tarda unos 5 minutos y la "
        "pantalla puede quedarse quieta un rato: es normal.",
    [PW_TEXT_STEAM_DOWNLOADED] = "{1} MB descargados",
    [PW_TEXT_STEAM_ELAPSED] = "Tiempo transcurrido: {1}",
    [PW_TEXT_STEAM_SLOW] = "Está tardando más de lo habitual. Puedes seguir esperando o volver a intentarlo.",
    [PW_TEXT_STEAM_SCAN_TITLE] = "Ten el móvil a mano",
    [PW_TEXT_STEAM_SCAN_BODY] = "Steam está abriendo su página de inicio de sesión. Abre la aplicación de Steam en tu "
        "móvil, toca el escudo y escanea el código QR de la pantalla.",
    [PW_TEXT_STEAM_CANCEL_NOTE] = "Para cancelar, mantén pulsado OPTIONS durante 2 segundos.",
    [PW_TEXT_STEAM_SIGNED_IN] = "Has iniciado sesión",
    [PW_TEXT_STEAM_SIGNED_IN_AS] = "Has iniciado sesión como {1}",
    [PW_TEXT_STEAM_REOPEN_BODY] = "Steam se cierra ahora y se vuelve a abrir en Big Picture: pensado para el mando, "
        "con ajustes adaptados a la PS5. A partir de ahora, Steam se abre directamente en Big Picture.",
    [PW_TEXT_STEAM_DONE_BODY] = "A partir de ahora, Steam se abre directamente en Big Picture: pensado para el mando, "
        "con ajustes adaptados a la PS5. No tendrás que volver a iniciar sesión.",
    [PW_TEXT_STEAM_OPENING_IN] = "Se abre en {1} s",
};

static const char *const text_de[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "Willkommen bei MihawkOS",
    [PW_TEXT_WELCOME_BODY] = "Machen wir deine PS5 bereit für Windows-Spiele. Das dauert etwa zehn Minuten.",
    [PW_TEXT_GET_STARTED] = "Los geht’s",
    [PW_TEXT_SKIP_SETUP] = "Einrichtung überspringen",
    [PW_TEXT_LANGUAGE_TITLE] = "Wähle deine Sprache",
    [PW_TEXT_LANGUAGE_BODY] = "Deine Windows-Spiele verwenden sie auch, samt Tastaturlayout. Der Desktop von Mihawk OS "
        "bleibt auf Englisch.",
    [PW_TEXT_METHOD_TITLE] = "Wähle die Installationsart",
    [PW_TEXT_METHOD_BODY] = "MihawkOS braucht seine Windows-Laufzeitumgebung, etwa 1,2 GB.",
    [PW_TEXT_METHOD_USB] = "USB-Laufwerk",
    [PW_TEXT_METHOD_USB_BODY] = "Kopiere die Dateien an einem beliebigen Computer auf ein USB-Laufwerk.",
    [PW_TEXT_METHOD_FTP] = "FTP",
    [PW_TEXT_METHOD_FTP_BODY] = "Kopiere die Dateien über dein Netzwerk auf deine PS5.",
    [PW_TEXT_METHOD_INTERNET] = "Internet",
    [PW_TEXT_METHOD_INTERNET_BODY] = "Direkt auf deine PS5 herunterladen.",
    [PW_TEXT_COMING_SOON] = "Demnächst",
    [PW_TEXT_METHOD_INSTALLED] = "Die Windows-Laufzeitumgebung ist bereits installiert.",
    [PW_TEXT_CONTINUE] = "Weiter",
    [PW_TEXT_QR_CAPTION] = "Scanne den Code für eine Schritt-für-Schritt-Anleitung auf deinem Handy.",
    [PW_TEXT_USB_TITLE] = "USB-Laufwerk vorbereiten",
    [PW_TEXT_USB_STEP1] = "Formatiere an einem Computer ein USB-Laufwerk als exFAT mit MBR-Partitionstabelle. Dabei wird "
        "das Laufwerk gelöscht.",
    [PW_TEXT_USB_STEP2] = "Lade MihawkOS herunter und kopiere den Ordner mihawkos in die oberste Ebene des Laufwerks.",
    [PW_TEXT_USB_STEP3] = "Stecke das Laufwerk in einen USB-Anschluss deiner PS5.",
    [PW_TEXT_USB_STEP4] = "Wähle Laufwerk prüfen.",
    [PW_TEXT_CHECK_DRIVE] = "Laufwerk prüfen",
    [PW_TEXT_FOUND] = "Das Laufwerk enthält {1} zum Installieren.",
    [PW_TEXT_INSTALL] = "Installieren",
    [PW_TEXT_RETRY] = "Erneut versuchen",
    [PW_TEXT_FTP_TITLE] = "Dateien per FTP kopieren",
    [PW_TEXT_FTP_STEP1] = "Starte auf deiner PS5 einen FTP-Server wie den Payload ftpsrv (oft Port 2121).",
    [PW_TEXT_FTP_STEP2] = "Entpacke an einem Computer die Datei usb-mihawkos von MihawkOS.",
    [PW_TEXT_FTP_STEP3] = "Kopiere mit einem FTP-Programm wie FileZilla die Dateien aus mihawkos nach "
        "/data/ps5proton/install.",
    [PW_TEXT_FTP_STEP4] = "Wähle Dateien prüfen.",
    [PW_TEXT_CHECK_FILES] = "Dateien prüfen",
    [PW_TEXT_FTP_FOUND] = "Der Installationsordner enthält {1} zum Installieren.",
    [PW_TEXT_ERR_FTP_EMPTY] = "In /data/ps5proton/install liegen keine Laufzeitdateien. Kopiere die Dateien aus dem "
        "Ordner mihawkos dorthin, nicht den Ordner selbst, und versuche es erneut.",
    [PW_TEXT_ERR_FTP_PARTIAL] = "/data/ps5proton/install enthält {1} von {2} Teilen. Kopiere die fehlenden Dateien "
        "und versuche es erneut.",
    [PW_TEXT_FTP_INSTALLING_BODY] = "Die Dateien werden aus /data/ps5proton/install gelesen. Das dauert etwa eine "
        "Minute.",
    [PW_TEXT_ERR_FTP_READ] = "Eine Datei in /data/ps5proton/install konnte nicht gelesen werden. Kopiere die Dateien "
        "noch einmal und versuche es erneut. Bereits Installiertes bleibt erhalten.",
    [PW_TEXT_ERR_FTP_DAMAGED] = "Eine Datei in /data/ps5proton/install ist beschädigt oder stammt aus einer anderen "
        "Version. Kopiere die Dateien aus dem Ordner mihawkos noch einmal und versuche es erneut.",
    [PW_TEXT_ERR_NO_STICK] = "Kein USB-Laufwerk gefunden. Stecke es ein, warte ein paar Sekunden und versuche es erneut. "
        "Steckt es schon, formatiere es als exFAT mit MBR: Die PS5 liest keine GPT- oder NTFS-Laufwerke.",
    [PW_TEXT_ERR_NO_FOLDER] = "Auf der obersten Ebene des Laufwerks liegt kein Ordner mihawkos. Kopiere den Ordner auf "
        "das Laufwerk und versuche es erneut.",
    [PW_TEXT_ERR_PARTIAL] = "Der Ordner mihawkos ist unvollständig: Er enthält {1} von {2} Teilen. Kopiere den ganzen Ordner "
        "noch einmal und versuche es erneut.",
    [PW_TEXT_ERR_NO_SPACE] = "Deine PS5 braucht {1} freien Speicher und hat {2}. Lösche etwas und versuche es erneut.",
    [PW_TEXT_ERR_READ] = "Das USB-Laufwerk konnte nicht gelesen werden. Prüfe, ob es noch steckt, und versuche es erneut. "
        "Bereits Installiertes bleibt erhalten.",
    [PW_TEXT_ERR_DAMAGED] = "Eine Datei auf dem Laufwerk ist beschädigt oder stammt aus einer anderen Version. Kopiere "
        "den Ordner mihawkos noch einmal und versuche es erneut.",
    [PW_TEXT_ERR_FULL] = "Der Speicher deiner PS5 ist voll. Lösche etwas und versuche es erneut. Bereits Installiertes "
        "bleibt erhalten.",
    [PW_TEXT_ERR_WRITE] = "Der Speicher deiner PS5 hat einen Schreibvorgang abgelehnt. Versuche es erneut; passiert es "
        "wieder, starte die PS5 neu.",
    [PW_TEXT_ERR_CANCELLED] = "Die Installation wurde gestoppt. Bereits Installiertes bleibt erhalten.",
    [PW_TEXT_ERR_BUILD] = "Diese Kopie von MihawkOS kann keine Windows-Laufzeitumgebung installieren. Installiere MihawkOS "
        "neu.",
    [PW_TEXT_INSTALLING_TITLE] = "Windows-Laufzeitumgebung wird installiert",
    [PW_TEXT_INSTALLING_BODY] = "Lass das Laufwerk eingesteckt. Das dauert etwa eine Minute.",
    [PW_TEXT_INSTALLING_CHECKING] = "Bereits Installiertes wird geprüft…",
    [PW_TEXT_INSTALLING_STATS] = "{1} von {2} MB · {3} MB/s",
    [PW_TEXT_INSTALLED] = "Installiert.",
    [PW_TEXT_STOP] = "Stoppen",
    [PW_TEXT_PREPARE_TITLE] = "Mihawk OS wird vorbereitet",
    [PW_TEXT_PREPARE_BODY] = "Der erste Start legt dein Windows-Laufwerk an. Das dauert ein paar Minuten, und der Bildschirm "
        "kann eine Weile stillstehen. Er endet von selbst und kehrt hierher zurück.",
    [PW_TEXT_PREPARE_NOTE] = "Beim ersten Start funktioniert der Controller in Spielen nicht. Deshalb beendet MihawkOS "
        "ihn, sobald das Laufwerk bereit ist.",
    [PW_TEXT_PREPARE_START] = "Starten",
    [PW_TEXT_PREPARE_FAILED] = "Der erste Start wurde nicht abgeschlossen. Versuche es erneut: Er macht dort weiter, wo "
        "er aufgehört hat.",
    [PW_TEXT_BUILD_TITLE] = "Dein Windows-Laufwerk wird angelegt",
    [PW_TEXT_BUILD_BODY] = "Das dauert etwa eine Minute. Sobald das Laufwerk bereit ist, kehrt MihawkOS von selbst "
        "zur Einrichtung zurück.",
    [PW_TEXT_BUILD_SETTINGS] = "Windows wird eingerichtet",
    [PW_TEXT_BUILD_DOTNET] = ".NET-Unterstützung wird installiert",
    [PW_TEXT_BUILD_FINISHING] = "Wird abgeschlossen",
    [PW_TEXT_DONE_TITLE] = "Alles bereit",
    [PW_TEXT_DONE_BODY] = "Mihawk OS ist bereit. Diese Einrichtung kannst du in der Bibliothek unter Settings wiederholen.",
    [PW_TEXT_ENTER] = "Zu Mihawk OS",
    [PW_TEXT_HINT_SELECT] = "Auswählen",
    [PW_TEXT_HINT_BACK] = "Zurück",
    [PW_TEXT_HINT_MOVE] = "Bewegen",
    [PW_TEXT_STEP_OF] = "Schritt {1} von {2}",
    [PW_TEXT_GIGABYTES] = "{1} GB",
    [PW_TEXT_DECIMAL] = ",",
    [PW_TEXT_STEAM_TITLE] = "Steam einrichten",
    [PW_TEXT_STEAM_BODY] = "Steam ist die App von Valve für deine PC-Spiele. Beim ersten Mal passieren vier Dinge:",
    [PW_TEXT_STEAM_STEP_DOWNLOAD] = "Herunterladen",
    [PW_TEXT_STEAM_STEP_DOWNLOAD_BODY] = "Der Steam-Installer kommt direkt von den Servern von Valve.",
    [PW_TEXT_STEAM_STEP_INSTALL] = "Installieren",
    [PW_TEXT_STEAM_STEP_INSTALL_BODY] = "MihawkOS installiert Steam in unter einer Minute auf deinem Windows-Laufwerk.",
    [PW_TEXT_STEAM_STEP_UPDATE] = "Erster Start",
    [PW_TEXT_STEAM_STEP_UPDATE_BODY] = "Steam aktualisiert sich selbst. Das dauert etwa 5 Minuten.",
    [PW_TEXT_STEAM_STEP_SIGNIN] = "Anmelden",
    [PW_TEXT_STEAM_STEP_SIGNIN_BODY] = "Scanne einen QR-Code mit der Steam-App auf deinem Handy.",
    [PW_TEXT_STEAM_NOTE] = "Eine Internetverbindung ist nötig. Insgesamt etwa 10 Minuten. MihawkOS ist nicht mit "
        "Valve verbunden.",
    [PW_TEXT_STEAM_START] = "Starten",
    [PW_TEXT_STEAM_NOT_NOW] = "Nicht jetzt",
    [PW_TEXT_STEAM_DOWNLOADING] = "Steam wird heruntergeladen",
    [PW_TEXT_STEAM_DOWNLOADING_BODY] = "Von den Servern von Valve. MihawkOS installiert Steam, sobald der Download "
        "fertig ist; der Bildschirm wechselt kurz, das ist normal.",
    [PW_TEXT_STEAM_MEGABYTES] = "{1} von etwa {2} MB",
    [PW_TEXT_STEAM_STARTING] = "Installiert. Steam wird gestartet…",
    [PW_TEXT_STEAM_FAIL_TITLE] = "Die Einrichtung von Steam wurde angehalten",
    [PW_TEXT_STEAM_ERR_OFFLINE] = "Deine PS5 ist nicht mit dem Internet verbunden. Prüfe ihre Netzwerkeinstellungen "
        "und versuche es erneut.",
    [PW_TEXT_STEAM_ERR_CLOCK] = "Datum oder Uhrzeit deiner PS5 stimmen nicht, deshalb wurde der sichere Download "
        "abgelehnt. Stelle Datum und Uhrzeit in den Einstellungen der PS5 ein und versuche es erneut.",
    [PW_TEXT_STEAM_ERR_DOWNLOAD] = "Der Steam-Installer konnte nicht von den Servern von Valve geladen werden. "
        "Versuche es gleich noch einmal.",
    [PW_TEXT_STEAM_ERR_INSTALL] = "Der Steam-Installer wurde nicht abgeschlossen. Versuche es erneut: Der Download "
        "bleibt erhalten.",
    [PW_TEXT_STEAM_ERR_STALLED] = "Das Update von Steam kam lange nicht voran, deshalb wurde Steam geschlossen. "
        "Versuche es erneut: Es macht dort weiter, wo es aufgehört hat.",
    [PW_TEXT_STEAM_ERR_CANCELLED] = "Die Anmeldung wurde abgebrochen. Versuche es erneut, wann immer du bereit bist.",
    [PW_TEXT_STEAM_ERR_TIMEOUT] = "Innerhalb von 30 Minuten kam keine Anmeldung, deshalb wurde Steam geschlossen. "
        "Versuche es erneut, wann immer du bereit bist.",
    [PW_TEXT_STEAM_ERR_GUARD] = "Steam hat nach einem Steam-Guard-Code gefragt. Versuche es erneut und nutze "
        "stattdessen den QR-Code: Tippe in der Steam-App auf deinem Handy auf das Schild und scanne den Code.",
    [PW_TEXT_STEAM_ERR_ENDED] = "Steam wurde vor der Anmeldung geschlossen. Versuche es erneut.",
    [PW_TEXT_STEAM_INSTALLING_TITLE] = "Steam wird installiert",
    [PW_TEXT_STEAM_INSTALLING_BODY] = "Der Installer von Valve richtet Steam auf deinem Windows-Laufwerk ein. Das "
        "dauert weniger als eine Minute.",
    [PW_TEXT_STEAM_UPDATING_TITLE] = "Steam aktualisiert sich",
    [PW_TEXT_STEAM_UPDATING_BODY] = "Beim ersten Start lädt Steam seine neueste Version. Das dauert etwa 5 Minuten, "
        "und der Bildschirm kann eine Weile stillstehen: Das ist normal.",
    [PW_TEXT_STEAM_DOWNLOADED] = "{1} MB heruntergeladen",
    [PW_TEXT_STEAM_ELAPSED] = "Vergangene Zeit: {1}",
    [PW_TEXT_STEAM_SLOW] = "Das dauert länger als üblich. Du kannst weiter warten oder es erneut versuchen.",
    [PW_TEXT_STEAM_SCAN_TITLE] = "Halte dein Handy bereit",
    [PW_TEXT_STEAM_SCAN_BODY] = "Steam öffnet seine Anmeldeseite. Öffne die Steam-App auf deinem Handy, tippe auf das "
        "Schild und scanne den QR-Code auf dem Bildschirm.",
    [PW_TEXT_STEAM_CANCEL_NOTE] = "Zum Abbrechen OPTIONS 2 Sekunden lang gedrückt halten.",
    [PW_TEXT_STEAM_SIGNED_IN] = "Du bist angemeldet",
    [PW_TEXT_STEAM_SIGNED_IN_AS] = "Du bist als {1} angemeldet",
    [PW_TEXT_STEAM_REOPEN_BODY] = "Steam wird jetzt geschlossen und in Big Picture wieder geöffnet: gemacht für den "
        "Controller, mit Einstellungen, die auf die PS5 abgestimmt sind. Ab jetzt öffnet sich Steam direkt in Big "
        "Picture.",
    [PW_TEXT_STEAM_DONE_BODY] = "Ab jetzt öffnet sich Steam direkt in Big Picture: gemacht für den Controller, mit "
        "Einstellungen, die auf die PS5 abgestimmt sind. Du musst dich nicht noch einmal anmelden.",
    [PW_TEXT_STEAM_OPENING_IN] = "Wird in {1} s geöffnet",
};

static const char *const text_it[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "Benvenuto in MihawkOS",
    [PW_TEXT_WELCOME_BODY] = "Prepariamo la tua PS5 per i giochi Windows. Ci vogliono circa dieci minuti.",
    [PW_TEXT_GET_STARTED] = "Inizia",
    [PW_TEXT_SKIP_SETUP] = "Salta la configurazione",
    [PW_TEXT_LANGUAGE_TITLE] = "Scegli la lingua",
    [PW_TEXT_LANGUAGE_BODY] = "Anche i tuoi giochi Windows la useranno, con il suo layout di tastiera. Il desktop di Mihawk "
        "OS resta in inglese.",
    [PW_TEXT_METHOD_TITLE] = "Scegli come installare",
    [PW_TEXT_METHOD_BODY] = "MihawkOS ha bisogno del suo ambiente Windows, circa 1,2 GB.",
    [PW_TEXT_METHOD_USB] = "Unità USB",
    [PW_TEXT_METHOD_USB_BODY] = "Copia i file su un’unità USB da qualsiasi computer.",
    [PW_TEXT_METHOD_FTP] = "FTP",
    [PW_TEXT_METHOD_FTP_BODY] = "Copia i file sulla PS5 attraverso la tua rete.",
    [PW_TEXT_METHOD_INTERNET] = "Internet",
    [PW_TEXT_METHOD_INTERNET_BODY] = "Scarica direttamente sulla tua PS5.",
    [PW_TEXT_COMING_SOON] = "Prossimamente",
    [PW_TEXT_METHOD_INSTALLED] = "L’ambiente Windows è già installato.",
    [PW_TEXT_CONTINUE] = "Continua",
    [PW_TEXT_QR_CAPTION] = "Inquadra il codice per le istruzioni passo passo sul telefono.",
    [PW_TEXT_USB_TITLE] = "Prepara un’unità USB",
    [PW_TEXT_USB_STEP1] = "Su un computer, formatta un’unità USB in exFAT con tabella delle partizioni MBR. L’unità verrà "
        "cancellata.",
    [PW_TEXT_USB_STEP2] = "Scarica MihawkOS e copia la sua cartella mihawkos nella radice dell’unità.",
    [PW_TEXT_USB_STEP3] = "Collega l’unità a una porta USB della PS5.",
    [PW_TEXT_USB_STEP4] = "Scegli Controlla unità.",
    [PW_TEXT_CHECK_DRIVE] = "Controlla unità",
    [PW_TEXT_FOUND] = "L’unità contiene {1} da installare.",
    [PW_TEXT_INSTALL] = "Installa",
    [PW_TEXT_RETRY] = "Riprova",
    [PW_TEXT_FTP_TITLE] = "Copia i file via FTP",
    [PW_TEXT_FTP_STEP1] = "Sulla PS5, avvia un server FTP come il payload ftpsrv (spesso porta 2121).",
    [PW_TEXT_FTP_STEP2] = "Su un computer, estrai il file usb-mihawkos di MihawkOS.",
    [PW_TEXT_FTP_STEP3] = "Con un client FTP come FileZilla, copia i file della cartella mihawkos in "
        "/data/ps5proton/install.",
    [PW_TEXT_FTP_STEP4] = "Scegli Controlla file.",
    [PW_TEXT_CHECK_FILES] = "Controlla file",
    [PW_TEXT_FTP_FOUND] = "La cartella di installazione contiene {1} da installare.",
    [PW_TEXT_ERR_FTP_EMPTY] = "Nessun file dell’ambiente in /data/ps5proton/install. Copia lì i file della cartella "
        "mihawkos, non la cartella stessa, e riprova.",
    [PW_TEXT_ERR_FTP_PARTIAL] = "/data/ps5proton/install contiene {1} parti su {2}. Copia i file mancanti e riprova.",
    [PW_TEXT_FTP_INSTALLING_BODY] = "I file vengono letti da /data/ps5proton/install. Ci vuole circa un minuto.",
    [PW_TEXT_ERR_FTP_READ] = "Impossibile leggere un file in /data/ps5proton/install. Copia di nuovo i file e "
        "riprova. Ciò che è già installato viene conservato.",
    [PW_TEXT_ERR_FTP_DAMAGED] = "Un file in /data/ps5proton/install è danneggiato o di un’altra versione. Copia di "
        "nuovo i file della cartella mihawkos e riprova.",
    [PW_TEXT_ERR_NO_STICK] = "Nessuna unità USB trovata. Collegala, attendi qualche secondo e riprova. Se è già collegata, "
        "formattala in exFAT con MBR: la PS5 non legge le unità GPT o NTFS.",
    [PW_TEXT_ERR_NO_FOLDER] = "L’unità non ha una cartella mihawkos nella radice. Copia la cartella sull’unità e riprova.",
    [PW_TEXT_ERR_PARTIAL] = "La cartella mihawkos è incompleta: contiene {1} parti su {2}. Copia di nuovo l’intera cartella "
        "e riprova.",
    [PW_TEXT_ERR_NO_SPACE] = "La PS5 ha bisogno di {1} liberi e ne ha {2}. Elimina qualcosa e riprova.",
    [PW_TEXT_ERR_READ] = "Impossibile leggere l’unità USB. Verifica che sia ancora collegata e riprova. Ciò che è già installato "
        "viene conservato.",
    [PW_TEXT_ERR_DAMAGED] = "Un file sull’unità è danneggiato o di un’altra versione. Copia di nuovo la cartella mihawkos "
        "e riprova.",
    [PW_TEXT_ERR_FULL] = "La memoria della PS5 è piena. Elimina qualcosa e riprova. Ciò che è già installato viene conservato.",
    [PW_TEXT_ERR_WRITE] = "La memoria della PS5 ha rifiutato una scrittura. Riprova; se succede di nuovo, riavvia la PS5.",
    [PW_TEXT_ERR_CANCELLED] = "Installazione interrotta. Ciò che è già installato viene conservato.",
    [PW_TEXT_ERR_BUILD] = "Questa copia di MihawkOS non può installare un ambiente Windows. Reinstalla MihawkOS.",
    [PW_TEXT_INSTALLING_TITLE] = "Installazione dell’ambiente Windows",
    [PW_TEXT_INSTALLING_BODY] = "Lascia l’unità collegata. Ci vuole circa un minuto.",
    [PW_TEXT_INSTALLING_CHECKING] = "Controllo di ciò che è già installato…",
    [PW_TEXT_INSTALLING_STATS] = "{1} di {2} MB · {3} MB/s",
    [PW_TEXT_INSTALLED] = "Installato.",
    [PW_TEXT_STOP] = "Interrompi",
    [PW_TEXT_PREPARE_TITLE] = "Preparazione di Mihawk OS",
    [PW_TEXT_PREPARE_BODY] = "Il primo avvio crea il tuo disco Windows. Ci vogliono alcuni minuti e lo schermo può restare "
        "fermo per un po’. Termina da solo e torna qui.",
    [PW_TEXT_PREPARE_NOTE] = "Durante questo primo avvio il controller non funziona nei giochi, quindi MihawkOS lo termina "
        "appena il disco è pronto.",
    [PW_TEXT_PREPARE_START] = "Avvia",
    [PW_TEXT_PREPARE_FAILED] = "Il primo avvio non è terminato. Riprova: riprende da dove si era fermato.",
    [PW_TEXT_BUILD_TITLE] = "Creazione del tuo disco Windows",
    [PW_TEXT_BUILD_BODY] = "Ci vuole circa un minuto. Quando il disco è pronto, MihawkOS torna da solo alla "
        "configurazione.",
    [PW_TEXT_BUILD_SETTINGS] = "Configurazione di Windows",
    [PW_TEXT_BUILD_DOTNET] = "Installazione del supporto .NET",
    [PW_TEXT_BUILD_FINISHING] = "Completamento",
    [PW_TEXT_DONE_TITLE] = "Tutto pronto",
    [PW_TEXT_DONE_BODY] = "Mihawk OS è pronto. Puoi ripetere questa configurazione da Settings, nella libreria.",
    [PW_TEXT_ENTER] = "Entra in Mihawk OS",
    [PW_TEXT_HINT_SELECT] = "Scegli",
    [PW_TEXT_HINT_BACK] = "Indietro",
    [PW_TEXT_HINT_MOVE] = "Sposta",
    [PW_TEXT_STEP_OF] = "Passo {1} di {2}",
    [PW_TEXT_GIGABYTES] = "{1} GB",
    [PW_TEXT_DECIMAL] = ",",
    [PW_TEXT_STEAM_TITLE] = "Configura Steam",
    [PW_TEXT_STEAM_BODY] = "Steam è l’app di Valve per i tuoi giochi PC. La prima volta succedono quattro cose:",
    [PW_TEXT_STEAM_STEP_DOWNLOAD] = "Download",
    [PW_TEXT_STEAM_STEP_DOWNLOAD_BODY] = "Il programma di installazione di Steam arriva direttamente dai server di "
        "Valve.",
    [PW_TEXT_STEAM_STEP_INSTALL] = "Installazione",
    [PW_TEXT_STEAM_STEP_INSTALL_BODY] = "MihawkOS lo installa sul tuo disco Windows in meno di un minuto.",
    [PW_TEXT_STEAM_STEP_UPDATE] = "Primo avvio",
    [PW_TEXT_STEAM_STEP_UPDATE_BODY] = "Steam si aggiorna da solo. Ci vogliono circa 5 minuti.",
    [PW_TEXT_STEAM_STEP_SIGNIN] = "Accesso",
    [PW_TEXT_STEAM_STEP_SIGNIN_BODY] = "Inquadra un codice QR con l’app di Steam sul telefono.",
    [PW_TEXT_STEAM_NOTE] = "Serve una connessione a Internet. Circa 10 minuti in tutto. MihawkOS non è affiliato a "
        "Valve.",
    [PW_TEXT_STEAM_START] = "Inizia",
    [PW_TEXT_STEAM_NOT_NOW] = "Non ora",
    [PW_TEXT_STEAM_DOWNLOADING] = "Download di Steam",
    [PW_TEXT_STEAM_DOWNLOADING_BODY] = "Dai server di Valve. MihawkOS installa Steam appena il download è finito; lo "
        "schermo cambia per un attimo, è normale.",
    [PW_TEXT_STEAM_MEGABYTES] = "{1} di circa {2} MB",
    [PW_TEXT_STEAM_STARTING] = "Installato. Avvio di Steam…",
    [PW_TEXT_STEAM_FAIL_TITLE] = "La configurazione di Steam si è interrotta",
    [PW_TEXT_STEAM_ERR_OFFLINE] = "La PS5 non è connessa a Internet. Controlla le impostazioni di rete e riprova.",
    [PW_TEXT_STEAM_ERR_CLOCK] = "La data o l’ora della PS5 non sono corrette, quindi il download sicuro è stato "
        "rifiutato. Imposta data e ora nelle impostazioni della PS5 e riprova.",
    [PW_TEXT_STEAM_ERR_DOWNLOAD] = "Impossibile scaricare il programma di installazione di Steam dai server di Valve. "
        "Riprova tra un attimo.",
    [PW_TEXT_STEAM_ERR_INSTALL] = "Il programma di installazione di Steam non è terminato. Riprova: il download viene "
        "conservato.",
    [PW_TEXT_STEAM_ERR_STALLED] = "L’aggiornamento di Steam è rimasto fermo a lungo, quindi Steam è stato chiuso. "
        "Riprova: riprende da dove si era fermato.",
    [PW_TEXT_STEAM_ERR_CANCELLED] = "L’accesso è stato annullato. Riprova quando vuoi.",
    [PW_TEXT_STEAM_ERR_TIMEOUT] = "Nessun accesso entro 30 minuti, quindi Steam è stato chiuso. Riprova quando vuoi.",
    [PW_TEXT_STEAM_ERR_GUARD] = "Steam ha chiesto un codice Steam Guard. Riprova e usa invece il codice QR: nell’app "
        "di Steam sul telefono, tocca lo scudo e inquadra il codice.",
    [PW_TEXT_STEAM_ERR_ENDED] = "Steam si è chiuso prima dell’accesso. Riprova.",
    [PW_TEXT_STEAM_INSTALLING_TITLE] = "Installazione di Steam",
    [PW_TEXT_STEAM_INSTALLING_BODY] = "Il programma di installazione di Valve sta mettendo Steam sul tuo disco "
        "Windows. Ci vuole meno di un minuto.",
    [PW_TEXT_STEAM_UPDATING_TITLE] = "Steam si sta aggiornando",
    [PW_TEXT_STEAM_UPDATING_BODY] = "Il primo avvio scarica l’ultima versione di Steam. Ci vogliono circa 5 minuti e "
        "lo schermo può restare fermo per un po’: è normale.",
    [PW_TEXT_STEAM_DOWNLOADED] = "{1} MB scaricati",
    [PW_TEXT_STEAM_ELAPSED] = "Tempo trascorso: {1}",
    [PW_TEXT_STEAM_SLOW] = "Ci sta mettendo più del solito. Puoi continuare ad aspettare o riprovare.",
    [PW_TEXT_STEAM_SCAN_TITLE] = "Tieni pronto il telefono",
    [PW_TEXT_STEAM_SCAN_BODY] = "Steam sta aprendo la pagina di accesso. Apri l’app di Steam sul telefono, tocca lo "
        "scudo e inquadra il codice QR sullo schermo.",
    [PW_TEXT_STEAM_CANCEL_NOTE] = "Per annullare, tieni premuto OPTIONS per 2 secondi.",
    [PW_TEXT_STEAM_SIGNED_IN] = "Accesso effettuato",
    [PW_TEXT_STEAM_SIGNED_IN_AS] = "Accesso effettuato come {1}",
    [PW_TEXT_STEAM_REOPEN_BODY] = "Steam ora si chiude e si riapre in Big Picture: pensato per il controller, con "
        "impostazioni adatte alla PS5. D’ora in poi Steam si apre direttamente in Big Picture.",
    [PW_TEXT_STEAM_DONE_BODY] = "D’ora in poi Steam si apre direttamente in Big Picture: pensato per il controller, "
        "con impostazioni adatte alla PS5. Non dovrai più accedere.",
    [PW_TEXT_STEAM_OPENING_IN] = "Si apre tra {1} s",
};

static const char *const text_ru[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "Добро пожаловать в MihawkOS",
    [PW_TEXT_WELCOME_BODY] = "Подготовим вашу PS5 к играм для Windows. Это займёт около десяти минут.",
    [PW_TEXT_GET_STARTED] = "Начать",
    [PW_TEXT_SKIP_SETUP] = "Пропустить настройку",
    [PW_TEXT_LANGUAGE_TITLE] = "Выберите язык",
    [PW_TEXT_LANGUAGE_BODY] = "Ваши игры для Windows тоже будут его использовать вместе с раскладкой клавиатуры. Рабочий "
        "стол Mihawk OS останется на английском.",
    [PW_TEXT_METHOD_TITLE] = "Выберите способ установки",
    [PW_TEXT_METHOD_BODY] = "MihawkOS нужна своя среда Windows, около 1,2 ГБ.",
    [PW_TEXT_METHOD_USB] = "USB-накопитель",
    [PW_TEXT_METHOD_USB_BODY] = "Скопируйте файлы на USB-накопитель на любом компьютере.",
    [PW_TEXT_METHOD_FTP] = "FTP",
    [PW_TEXT_METHOD_FTP_BODY] = "Скопируйте файлы на PS5 по своей сети.",
    [PW_TEXT_METHOD_INTERNET] = "Интернет",
    [PW_TEXT_METHOD_INTERNET_BODY] = "Загрузка прямо на вашу PS5.",
    [PW_TEXT_COMING_SOON] = "Скоро",
    [PW_TEXT_METHOD_INSTALLED] = "Среда Windows уже установлена.",
    [PW_TEXT_CONTINUE] = "Продолжить",
    [PW_TEXT_QR_CAPTION] = "Отсканируйте код, чтобы открыть пошаговую инструкцию на телефоне.",
    [PW_TEXT_USB_TITLE] = "Подготовьте USB-накопитель",
    [PW_TEXT_USB_STEP1] = "На компьютере отформатируйте USB-накопитель в exFAT с таблицей разделов MBR. Все данные на нём "
        "будут удалены.",
    [PW_TEXT_USB_STEP2] = "Загрузите MihawkOS и скопируйте его папку mihawkos в корень накопителя.",
    [PW_TEXT_USB_STEP3] = "Подключите накопитель к USB-порту PS5.",
    [PW_TEXT_USB_STEP4] = "Выберите «Проверить накопитель».",
    [PW_TEXT_CHECK_DRIVE] = "Проверить накопитель",
    [PW_TEXT_FOUND] = "На накопителе {1} для установки.",
    [PW_TEXT_INSTALL] = "Установить",
    [PW_TEXT_RETRY] = "Повторить",
    [PW_TEXT_FTP_TITLE] = "Скопируйте файлы по FTP",
    [PW_TEXT_FTP_STEP1] = "На PS5 запустите FTP-сервер, например payload ftpsrv (часто порт 2121).",
    [PW_TEXT_FTP_STEP2] = "На компьютере распакуйте файл usb-mihawkos из MihawkOS.",
    [PW_TEXT_FTP_STEP3] = "FTP-клиентом, например FileZilla, скопируйте файлы из папки mihawkos в "
        "/data/ps5proton/install.",
    [PW_TEXT_FTP_STEP4] = "Выберите «Проверить файлы».",
    [PW_TEXT_CHECK_FILES] = "Проверить файлы",
    [PW_TEXT_FTP_FOUND] = "В папке установки {1} для установки.",
    [PW_TEXT_ERR_FTP_EMPTY] = "В /data/ps5proton/install нет файлов среды. Скопируйте туда файлы из папки mihawkos, а "
        "не саму папку, и повторите попытку.",
    [PW_TEXT_ERR_FTP_PARTIAL] = "В /data/ps5proton/install частей: {1} из {2}. Скопируйте недостающие файлы и "
        "повторите попытку.",
    [PW_TEXT_FTP_INSTALLING_BODY] = "Файлы читаются из /data/ps5proton/install. Это займёт около минуты.",
    [PW_TEXT_ERR_FTP_READ] = "Не удалось прочитать файл в /data/ps5proton/install. Скопируйте файлы заново и "
        "повторите попытку. Уже установленное сохранится.",
    [PW_TEXT_ERR_FTP_DAMAGED] = "Файл в /data/ps5proton/install повреждён или от другой версии. Скопируйте файлы из "
        "папки mihawkos заново и повторите попытку.",
    [PW_TEXT_ERR_NO_STICK] = "USB-накопитель не найден. Подключите его, подождите несколько секунд и повторите попытку. "
        "Если он уже подключён, отформатируйте его в exFAT с MBR: PS5 не читает накопители GPT и NTFS.",
    [PW_TEXT_ERR_NO_FOLDER] = "В корне накопителя нет папки mihawkos. Скопируйте папку на накопитель и повторите попытку.",
    [PW_TEXT_ERR_PARTIAL] = "Папка mihawkos неполная, частей: {1} из {2}. Скопируйте всю папку заново и повторите попытку.",
    [PW_TEXT_ERR_NO_SPACE] = "PS5 нужно {1} свободного места, а доступно {2}. Удалите что-нибудь и повторите попытку.",
    [PW_TEXT_ERR_READ] = "Не удалось прочитать USB-накопитель. Убедитесь, что он подключён, и повторите попытку. Уже установленное "
        "сохранится.",
    [PW_TEXT_ERR_DAMAGED] = "Файл на накопителе повреждён или от другой версии. Скопируйте папку mihawkos заново и повторите "
        "попытку.",
    [PW_TEXT_ERR_FULL] = "Хранилище PS5 заполнено. Удалите что-нибудь и повторите попытку. Уже установленное сохранится.",
    [PW_TEXT_ERR_WRITE] = "Хранилище PS5 отказало в записи. Повторите попытку; если это повторится, перезапустите PS5.",
    [PW_TEXT_ERR_CANCELLED] = "Установка остановлена. Уже установленное сохранится.",
    [PW_TEXT_ERR_BUILD] = "Эта копия MihawkOS не может установить среду Windows. Переустановите MihawkOS.",
    [PW_TEXT_INSTALLING_TITLE] = "Установка среды Windows",
    [PW_TEXT_INSTALLING_BODY] = "Не отключайте накопитель. Это займёт около минуты.",
    [PW_TEXT_INSTALLING_CHECKING] = "Проверка уже установленного…",
    [PW_TEXT_INSTALLING_STATS] = "{1} из {2} МБ · {3} МБ/с",
    [PW_TEXT_INSTALLED] = "Установлено.",
    [PW_TEXT_STOP] = "Остановить",
    [PW_TEXT_PREPARE_TITLE] = "Подготовка Mihawk OS",
    [PW_TEXT_PREPARE_BODY] = "При первом запуске создаётся ваш диск Windows. Это займёт несколько минут, и экран может "
        "какое-то время не меняться. Запуск завершится сам и вернётся сюда.",
    [PW_TEXT_PREPARE_NOTE] = "Во время первого запуска контроллер не работает в играх, поэтому MihawkOS завершает его, "
        "как только диск готов.",
    [PW_TEXT_PREPARE_START] = "Запустить",
    [PW_TEXT_PREPARE_FAILED] = "Первый запуск не завершился. Повторите попытку: он продолжится с того места, где остановился.",
    [PW_TEXT_BUILD_TITLE] = "Создание вашего диска Windows",
    [PW_TEXT_BUILD_BODY] = "Это займёт около минуты. Когда диск будет готов, MihawkOS сам вернётся к настройке.",
    [PW_TEXT_BUILD_SETTINGS] = "Настройка Windows",
    [PW_TEXT_BUILD_DOTNET] = "Установка поддержки .NET",
    [PW_TEXT_BUILD_FINISHING] = "Завершение",
    [PW_TEXT_DONE_TITLE] = "Всё готово",
    [PW_TEXT_DONE_BODY] = "Mihawk OS готова. Повторить эту настройку можно в разделе Settings в библиотеке.",
    [PW_TEXT_ENTER] = "Войти в Mihawk OS",
    [PW_TEXT_HINT_SELECT] = "Выбрать",
    [PW_TEXT_HINT_BACK] = "Назад",
    [PW_TEXT_HINT_MOVE] = "Перейти",
    [PW_TEXT_STEP_OF] = "Шаг {1} из {2}",
    [PW_TEXT_GIGABYTES] = "{1} ГБ",
    [PW_TEXT_DECIMAL] = ",",
    [PW_TEXT_STEAM_TITLE] = "Настройка Steam",
    [PW_TEXT_STEAM_BODY] = "Steam — приложение Valve для ваших игр для ПК. При первой настройке будет четыре шага:",
    [PW_TEXT_STEAM_STEP_DOWNLOAD] = "Загрузка",
    [PW_TEXT_STEAM_STEP_DOWNLOAD_BODY] = "Установщик Steam загружается прямо с серверов Valve.",
    [PW_TEXT_STEAM_STEP_INSTALL] = "Установка",
    [PW_TEXT_STEAM_STEP_INSTALL_BODY] = "MihawkOS устанавливает Steam на ваш диск Windows меньше чем за минуту.",
    [PW_TEXT_STEAM_STEP_UPDATE] = "Первый запуск",
    [PW_TEXT_STEAM_STEP_UPDATE_BODY] = "Steam обновляется сам. Это занимает около 5 минут.",
    [PW_TEXT_STEAM_STEP_SIGNIN] = "Вход",
    [PW_TEXT_STEAM_STEP_SIGNIN_BODY] = "Отсканируйте QR-код в приложении Steam на телефоне.",
    [PW_TEXT_STEAM_NOTE] = "Нужно подключение к интернету. Всего около 10 минут. MihawkOS не связана с Valve.",
    [PW_TEXT_STEAM_START] = "Начать",
    [PW_TEXT_STEAM_NOT_NOW] = "Не сейчас",
    [PW_TEXT_STEAM_DOWNLOADING] = "Загрузка Steam",
    [PW_TEXT_STEAM_DOWNLOADING_BODY] = "С серверов Valve. Как только загрузка закончится, MihawkOS установит Steam; "
        "экран на миг сменится, это нормально.",
    [PW_TEXT_STEAM_MEGABYTES] = "{1} из примерно {2} МБ",
    [PW_TEXT_STEAM_STARTING] = "Установлено. Запуск Steam…",
    [PW_TEXT_STEAM_FAIL_TITLE] = "Настройка Steam остановилась",
    [PW_TEXT_STEAM_ERR_OFFLINE] = "Ваша PS5 не подключена к интернету. Проверьте настройки сети и повторите попытку.",
    [PW_TEXT_STEAM_ERR_CLOCK] = "Дата или время на PS5 неверны, поэтому защищённая загрузка была отклонена. "
        "Установите дату и время в настройках PS5 и повторите попытку.",
    [PW_TEXT_STEAM_ERR_DOWNLOAD] = "Не удалось загрузить установщик Steam с серверов Valve. Повторите попытку чуть "
        "позже.",
    [PW_TEXT_STEAM_ERR_INSTALL] = "Установщик Steam не завершил работу. Повторите попытку: загруженный файл сохранён.",
    [PW_TEXT_STEAM_ERR_STALLED] = "Обновление Steam долго не продвигалось, поэтому Steam был закрыт. Повторите "
        "попытку: оно продолжится с того же места.",
    [PW_TEXT_STEAM_ERR_CANCELLED] = "Вход отменён. Повторите попытку, когда будете готовы.",
    [PW_TEXT_STEAM_ERR_TIMEOUT] = "За 30 минут вход не был выполнен, поэтому Steam был закрыт. Повторите попытку, "
        "когда будете готовы.",
    [PW_TEXT_STEAM_ERR_GUARD] = "Steam запросил код Steam Guard. Повторите попытку и войдите по QR-коду: в приложении "
        "Steam на телефоне нажмите на щит и отсканируйте код.",
    [PW_TEXT_STEAM_ERR_ENDED] = "Steam закрылся до входа. Повторите попытку.",
    [PW_TEXT_STEAM_INSTALLING_TITLE] = "Установка Steam",
    [PW_TEXT_STEAM_INSTALLING_BODY] = "Установщик Valve устанавливает Steam на ваш диск Windows. Это занимает меньше "
        "минуты.",
    [PW_TEXT_STEAM_UPDATING_TITLE] = "Steam обновляется",
    [PW_TEXT_STEAM_UPDATING_BODY] = "При первом запуске загружается последняя версия Steam. Это занимает около 5 "
        "минут, и экран может какое-то время не меняться: это нормально.",
    [PW_TEXT_STEAM_DOWNLOADED] = "Загружено {1} МБ",
    [PW_TEXT_STEAM_ELAPSED] = "Прошло: {1}",
    [PW_TEXT_STEAM_SLOW] = "Это занимает больше времени, чем обычно. Можно подождать ещё или повторить попытку.",
    [PW_TEXT_STEAM_SCAN_TITLE] = "Приготовьте телефон",
    [PW_TEXT_STEAM_SCAN_BODY] = "Steam открывает страницу входа. Откройте приложение Steam на телефоне, нажмите на "
        "щит и отсканируйте QR-код на экране.",
    [PW_TEXT_STEAM_CANCEL_NOTE] = "Чтобы отменить, удерживайте OPTIONS 2 секунды.",
    [PW_TEXT_STEAM_SIGNED_IN] = "Вход выполнен",
    [PW_TEXT_STEAM_SIGNED_IN_AS] = "Вход выполнен: {1}",
    [PW_TEXT_STEAM_REOPEN_BODY] = "Сейчас Steam закроется и снова откроется в режиме Big Picture: он создан для "
        "геймпада, а настройки подобраны для PS5. Теперь Steam всегда будет открываться сразу в Big Picture.",
    [PW_TEXT_STEAM_DONE_BODY] = "Теперь Steam открывается сразу в режиме Big Picture: он создан для геймпада, а "
        "настройки подобраны для PS5. Входить снова не придётся.",
    [PW_TEXT_STEAM_OPENING_IN] = "Открытие через {1} с",
};

static const char *const text_zh[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "欢迎使用 MihawkOS",
    [PW_TEXT_WELCOME_BODY] = "让我们为 Windows 游戏准备好你的 PS5。大约需要十分钟。",
    [PW_TEXT_GET_STARTED] = "开始",
    [PW_TEXT_SKIP_SETUP] = "跳过设置",
    [PW_TEXT_LANGUAGE_TITLE] = "选择语言",
    [PW_TEXT_LANGUAGE_BODY] = "你的 Windows 游戏也会使用该语言及其键盘布局。Mihawk OS 桌面仍为英文。",
    [PW_TEXT_METHOD_TITLE] = "选择安装方式",
    [PW_TEXT_METHOD_BODY] = "MihawkOS 需要安装其 Windows 运行环境，约 1.2 GB。",
    [PW_TEXT_METHOD_USB] = "U 盘",
    [PW_TEXT_METHOD_USB_BODY] = "在任意电脑上将文件复制到 U 盘。",
    [PW_TEXT_METHOD_FTP] = "FTP",
    [PW_TEXT_METHOD_FTP_BODY] = "通过网络将文件复制到 PS5。",
    [PW_TEXT_METHOD_INTERNET] = "互联网",
    [PW_TEXT_METHOD_INTERNET_BODY] = "直接下载到你的 PS5。",
    [PW_TEXT_COMING_SOON] = "即将推出",
    [PW_TEXT_METHOD_INSTALLED] = "Windows 运行环境已安装。",
    [PW_TEXT_CONTINUE] = "继续",
    [PW_TEXT_QR_CAPTION] = "扫码即可在手机上查看分步说明。",
    [PW_TEXT_USB_TITLE] = "准备 U 盘",
    [PW_TEXT_USB_STEP1] = "在电脑上将 U 盘格式化为 exFAT，分区表选择 MBR。这会清除 U 盘上的所有数据。",
    [PW_TEXT_USB_STEP2] = "下载 MihawkOS，并将其中的 mihawkos 文件夹复制到 U 盘根目录。",
    [PW_TEXT_USB_STEP3] = "将 U 盘插入 PS5 的 USB 接口。",
    [PW_TEXT_USB_STEP4] = "选择“检查 U 盘”。",
    [PW_TEXT_CHECK_DRIVE] = "检查 U 盘",
    [PW_TEXT_FOUND] = "U 盘中有 {1} 待安装。",
    [PW_TEXT_INSTALL] = "安装",
    [PW_TEXT_RETRY] = "重试",
    [PW_TEXT_FTP_TITLE] = "通过 FTP 复制文件",
    [PW_TEXT_FTP_STEP1] = "在 PS5 上启动 FTP 服务器，例如 ftpsrv payload（端口通常是 2121）。",
    [PW_TEXT_FTP_STEP2] = "在电脑上解压 MihawkOS 的 usb-mihawkos 文件。",
    [PW_TEXT_FTP_STEP3] = "用 FileZilla 等 FTP 客户端，将 mihawkos 文件夹中的文件复制到 /data/ps5proton/install。",
    [PW_TEXT_FTP_STEP4] = "选择“检查文件”。",
    [PW_TEXT_CHECK_FILES] = "检查文件",
    [PW_TEXT_FTP_FOUND] = "安装文件夹中有 {1} 待安装。",
    [PW_TEXT_ERR_FTP_EMPTY] = "/data/ps5proton/install 中没有运行环境文件。请将 mihawkos 文件夹中的文件（而不是文件夹本身）复制到该处后重试。",
    [PW_TEXT_ERR_FTP_PARTIAL] = "/data/ps5proton/install 中只有 {1}/{2} 个部分。请复制缺少的文件后重试。",
    [PW_TEXT_FTP_INSTALLING_BODY] = "正在从 /data/ps5proton/install 读取文件。大约需要一分钟。",
    [PW_TEXT_ERR_FTP_READ] = "无法读取 /data/ps5proton/install 中的某个文件。请重新复制文件后重试。已安装的内容会保留。",
    [PW_TEXT_ERR_FTP_DAMAGED] = "/data/ps5proton/install 中的某个文件已损坏或来自其他版本。请重新复制 mihawkos 文件夹中的文件后重试。",
    [PW_TEXT_ERR_NO_STICK] = "未找到 U 盘。请插入 U 盘，等待几秒后重试。如果已经插入，请将其格式化为 exFAT（MBR）：PS5 无法读取 GPT 或 NTFS 格式的磁盘。",
    [PW_TEXT_ERR_NO_FOLDER] = "U 盘根目录中没有 mihawkos 文件夹。请将该文件夹复制到 U 盘后重试。",
    [PW_TEXT_ERR_PARTIAL] = "mihawkos 文件夹不完整：只有 {1}/{2} 个部分。请重新复制整个文件夹后重试。",
    [PW_TEXT_ERR_NO_SPACE] = "PS5 需要 {1} 可用空间，当前只有 {2}。请删除一些内容后重试。",
    [PW_TEXT_ERR_READ] = "无法读取 U 盘。请确认它仍已插入，然后重试。已安装的内容会保留。",
    [PW_TEXT_ERR_DAMAGED] = "U 盘上的某个文件已损坏或来自其他版本。请重新复制 mihawkos 文件夹后重试。",
    [PW_TEXT_ERR_FULL] = "PS5 存储空间已满。请删除一些内容后重试。已安装的内容会保留。",
    [PW_TEXT_ERR_WRITE] = "PS5 存储拒绝了一次写入。请重试；如果再次发生，请重启 PS5。",
    [PW_TEXT_ERR_CANCELLED] = "安装已停止。已安装的内容会保留。",
    [PW_TEXT_ERR_BUILD] = "此 MihawkOS 副本无法安装 Windows 运行环境。请重新安装 MihawkOS。",
    [PW_TEXT_INSTALLING_TITLE] = "正在安装 Windows 运行环境",
    [PW_TEXT_INSTALLING_BODY] = "请勿拔出 U 盘。大约需要一分钟。",
    [PW_TEXT_INSTALLING_CHECKING] = "正在检查已安装的内容…",
    [PW_TEXT_INSTALLING_STATS] = "{1} / {2} MB · {3} MB/s",
    [PW_TEXT_INSTALLED] = "已安装。",
    [PW_TEXT_STOP] = "停止",
    [PW_TEXT_PREPARE_TITLE] = "正在准备 Mihawk OS",
    [PW_TEXT_PREPARE_BODY] = "首次启动会创建你的 Windows 磁盘。这需要几分钟，期间画面可能会静止一段时间。完成后会自动结束并返回这里。",
    [PW_TEXT_PREPARE_NOTE] = "首次启动期间，手柄在游戏中无法使用，因此磁盘准备就绪后 MihawkOS 会立即结束本次启动。",
    [PW_TEXT_PREPARE_START] = "启动",
    [PW_TEXT_PREPARE_FAILED] = "首次启动未完成。请重试：它会从中断的地方继续。",
    [PW_TEXT_BUILD_TITLE] = "正在创建你的 Windows 磁盘",
    [PW_TEXT_BUILD_BODY] = "大约需要一分钟。磁盘准备就绪后，MihawkOS 会自动返回设置。",
    [PW_TEXT_BUILD_SETTINGS] = "正在设置 Windows",
    [PW_TEXT_BUILD_DOTNET] = "正在安装 .NET 支持",
    [PW_TEXT_BUILD_FINISHING] = "即将完成",
    [PW_TEXT_DONE_TITLE] = "一切就绪",
    [PW_TEXT_DONE_BODY] = "Mihawk OS 已准备就绪。如需再次运行此设置，请在资料库中打开 Settings。",
    [PW_TEXT_ENTER] = "进入 Mihawk OS",
    [PW_TEXT_HINT_SELECT] = "选择",
    [PW_TEXT_HINT_BACK] = "返回",
    [PW_TEXT_HINT_MOVE] = "移动",
    [PW_TEXT_STEP_OF] = "第 {1} 步，共 {2} 步",
    [PW_TEXT_GIGABYTES] = "{1} GB",
    [PW_TEXT_DECIMAL] = ".",
    [PW_TEXT_STEAM_TITLE] = "设置 Steam",
    [PW_TEXT_STEAM_BODY] = "Steam 是 Valve 的电脑游戏应用。首次设置会经过四步：",
    [PW_TEXT_STEAM_STEP_DOWNLOAD] = "下载",
    [PW_TEXT_STEAM_STEP_DOWNLOAD_BODY] = "Steam 安装程序直接来自 Valve 的服务器。",
    [PW_TEXT_STEAM_STEP_INSTALL] = "安装",
    [PW_TEXT_STEAM_STEP_INSTALL_BODY] = "MihawkOS 会把它安装到你的 Windows 磁盘，不到一分钟。",
    [PW_TEXT_STEAM_STEP_UPDATE] = "首次启动",
    [PW_TEXT_STEAM_STEP_UPDATE_BODY] = "Steam 会自行更新，大约需要 5 分钟。",
    [PW_TEXT_STEAM_STEP_SIGNIN] = "登录",
    [PW_TEXT_STEAM_STEP_SIGNIN_BODY] = "用手机上的 Steam 应用扫描二维码。",
    [PW_TEXT_STEAM_NOTE] = "需要连接互联网。总共约 10 分钟。MihawkOS 与 Valve 无关联。",
    [PW_TEXT_STEAM_START] = "开始",
    [PW_TEXT_STEAM_NOT_NOW] = "以后再说",
    [PW_TEXT_STEAM_DOWNLOADING] = "正在下载 Steam",
    [PW_TEXT_STEAM_DOWNLOADING_BODY] = "来自 Valve 的服务器。下载完成后 MihawkOS 会立即安装 Steam；屏幕会短暂切换，这是正常的。",
    [PW_TEXT_STEAM_MEGABYTES] = "{1} MB，共约 {2} MB",
    [PW_TEXT_STEAM_STARTING] = "已安装。正在启动 Steam…",
    [PW_TEXT_STEAM_FAIL_TITLE] = "Steam 设置已停止",
    [PW_TEXT_STEAM_ERR_OFFLINE] = "你的 PS5 未连接到互联网。请检查网络设置，然后重试。",
    [PW_TEXT_STEAM_ERR_CLOCK] = "你的 PS5 日期或时间不正确，因此安全下载被拒绝。请在 PS5 设置中调整日期和时间，然后重试。",
    [PW_TEXT_STEAM_ERR_DOWNLOAD] = "无法从 Valve 的服务器下载 Steam 安装程序。请稍后重试。",
    [PW_TEXT_STEAM_ERR_INSTALL] = "Steam 安装程序未完成。请重试：已下载的文件会保留。",
    [PW_TEXT_STEAM_ERR_STALLED] = "Steam 的更新长时间没有进展，因此 Steam 已关闭。请重试：它会从中断处继续。",
    [PW_TEXT_STEAM_ERR_CANCELLED] = "登录已取消。准备好后请重试。",
    [PW_TEXT_STEAM_ERR_TIMEOUT] = "30 分钟内没有完成登录，因此 Steam 已关闭。准备好后请重试。",
    [PW_TEXT_STEAM_ERR_GUARD] = "Steam 要求输入 Steam 令牌验证码。请重试并改用二维码：在手机上的 Steam 应用中点按盾牌图标，然后扫描二维码。",
    [PW_TEXT_STEAM_ERR_ENDED] = "Steam 在你登录前关闭了。请重试。",
    [PW_TEXT_STEAM_INSTALLING_TITLE] = "正在安装 Steam",
    [PW_TEXT_STEAM_INSTALLING_BODY] = "Valve 的安装程序正在把 Steam 安装到你的 Windows 磁盘，不到一分钟。",
    [PW_TEXT_STEAM_UPDATING_TITLE] = "Steam 正在自行更新",
    [PW_TEXT_STEAM_UPDATING_BODY] = "首次启动会下载 Steam 的最新版本，大约需要 5 分钟。屏幕可能会静止一段时间，这是正常的。",
    [PW_TEXT_STEAM_DOWNLOADED] = "已下载 {1} MB",
    [PW_TEXT_STEAM_ELAPSED] = "已用时间：{1}",
    [PW_TEXT_STEAM_SLOW] = "这比平时花的时间更长。你可以继续等待，或者重试。",
    [PW_TEXT_STEAM_SCAN_TITLE] = "请准备好手机",
    [PW_TEXT_STEAM_SCAN_BODY] = "Steam 正在打开登录页面。在手机上打开 Steam 应用，点按盾牌图标，然后扫描屏幕上的二维码。",
    [PW_TEXT_STEAM_CANCEL_NOTE] = "要取消，请按住 OPTIONS 2 秒。",
    [PW_TEXT_STEAM_SIGNED_IN] = "已登录",
    [PW_TEXT_STEAM_SIGNED_IN_AS] = "已登录：{1}",
    [PW_TEXT_STEAM_REOPEN_BODY] = "Steam 现在会关闭，并以 Big Picture 模式重新打开：专为手柄设计，设置已针对 PS5 调整。今后 Steam 会直接以 Big Picture 打开。",
    [PW_TEXT_STEAM_DONE_BODY] = "今后 Steam 会直接以 Big Picture 模式打开：专为手柄设计，设置已针对 PS5 调整。你无需再次登录。",
    [PW_TEXT_STEAM_OPENING_IN] = "{1} 秒后打开",
};

static const char *const text_ja[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "MihawkOS へようこそ",
    [PW_TEXT_WELCOME_BODY] = "PS5 で Windows のゲームを遊ぶ準備をしましょう。所要時間は約 10 分です。",
    [PW_TEXT_GET_STARTED] = "はじめる",
    [PW_TEXT_SKIP_SETUP] = "セットアップをスキップ",
    [PW_TEXT_LANGUAGE_TITLE] = "言語を選択",
    [PW_TEXT_LANGUAGE_BODY] = "Windows のゲームでもこの言語とそのキーボード配列が使われます。Mihawk OS のデスクトップは英語のままです。",
    [PW_TEXT_METHOD_TITLE] = "インストール方法を選択",
    [PW_TEXT_METHOD_BODY] = "MihawkOS には Windows 実行環境（約 1.2 GB）が必要です。",
    [PW_TEXT_METHOD_USB] = "USB ドライブ",
    [PW_TEXT_METHOD_USB_BODY] = "お好きなパソコンで USB ドライブにファイルをコピーします。",
    [PW_TEXT_METHOD_FTP] = "FTP",
    [PW_TEXT_METHOD_FTP_BODY] = "ネットワーク経由で PS5 にファイルをコピーします。",
    [PW_TEXT_METHOD_INTERNET] = "インターネット",
    [PW_TEXT_METHOD_INTERNET_BODY] = "PS5 に直接ダウンロードします。",
    [PW_TEXT_COMING_SOON] = "近日対応",
    [PW_TEXT_METHOD_INSTALLED] = "Windows 実行環境はインストール済みです。",
    [PW_TEXT_CONTINUE] = "続ける",
    [PW_TEXT_QR_CAPTION] = "スマートフォンでスキャンすると、詳しい手順を確認できます。",
    [PW_TEXT_USB_TITLE] = "USB ドライブを準備する",
    [PW_TEXT_USB_STEP1] = "パソコンで USB ドライブを exFAT（パーティションテーブルは MBR）でフォーマットします。ドライブの内容は消去されます。",
    [PW_TEXT_USB_STEP2] = "MihawkOS をダウンロードし、mihawkos フォルダーをドライブの最上位にコピーします。",
    [PW_TEXT_USB_STEP3] = "ドライブを PS5 の USB ポートに接続します。",
    [PW_TEXT_USB_STEP4] = "「ドライブを確認」を選びます。",
    [PW_TEXT_CHECK_DRIVE] = "ドライブを確認",
    [PW_TEXT_FOUND] = "ドライブにインストールできる {1} があります。",
    [PW_TEXT_INSTALL] = "インストール",
    [PW_TEXT_RETRY] = "再試行",
    [PW_TEXT_FTP_TITLE] = "FTP でファイルをコピー",
    [PW_TEXT_FTP_STEP1] = "PS5 で ftpsrv などの FTP サーバーを起動します（ポートは多くの場合 2121）。",
    [PW_TEXT_FTP_STEP2] = "パソコンで MihawkOS の usb-mihawkos ファイルを展開します。",
    [PW_TEXT_FTP_STEP3] = "FileZilla などで、mihawkos 内のファイルを /data/ps5proton/install にコピーします。",
    [PW_TEXT_FTP_STEP4] = "「ファイルを確認」を選びます。",
    [PW_TEXT_CHECK_FILES] = "ファイルを確認",
    [PW_TEXT_FTP_FOUND] = "インストール フォルダーにインストールできる {1} があります。",
    [PW_TEXT_ERR_FTP_EMPTY] = "/data/ps5proton/install にランタイムのファイルがありません。mihawkos フォルダー内のファイル（フォルダー自体ではなく）をコピーしてから、もう一度お試しください。",
    [PW_TEXT_ERR_FTP_PARTIAL] = "/data/ps5proton/install のファイルが不完全です（{2} 個中 {1} 個）。足りないファイルをコピーしてから、もう一度お試しください。",
    [PW_TEXT_FTP_INSTALLING_BODY] = "/data/ps5proton/install からファイルを読み込んでいます。約 1 分かかります。",
    [PW_TEXT_ERR_FTP_READ] = "/data/ps5proton/install のファイルを読み取れません。ファイルをコピーし直してから、もう一度お試しください。インストール済みの内容は保持されます。",
    [PW_TEXT_ERR_FTP_DAMAGED] = "/data/ps5proton/install のファイルが壊れているか、別のバージョンのものです。mihawkos フォルダー内のファイルをコピーし直してから、もう一度お試しください。",
    [PW_TEXT_ERR_NO_STICK] = "USB ドライブが見つかりません。接続して数秒待ってから、もう一度お試しください。接続済みの場合は exFAT（MBR）でフォーマットしてください。PS5 は GPT や NTFS "
        "のドライブを読み取れません。",
    [PW_TEXT_ERR_NO_FOLDER] = "ドライブの最上位に mihawkos フォルダーがありません。フォルダーをドライブにコピーしてから、もう一度お試しください。",
    [PW_TEXT_ERR_PARTIAL] = "mihawkos フォルダーが不完全です（{2} 個中 {1} 個）。フォルダー全体をコピーし直してから、もう一度お試しください。",
    [PW_TEXT_ERR_NO_SPACE] = "PS5 に {1} の空き容量が必要ですが、{2} しかありません。不要なデータを削除してから、もう一度お試しください。",
    [PW_TEXT_ERR_READ] = "USB ドライブを読み取れません。接続されているか確認してから、もう一度お試しください。インストール済みの内容は保持されます。",
    [PW_TEXT_ERR_DAMAGED] = "ドライブ上のファイルが壊れているか、別のバージョンのものです。mihawkos フォルダーをコピーし直してから、もう一度お試しください。",
    [PW_TEXT_ERR_FULL] = "PS5 のストレージがいっぱいです。不要なデータを削除してから、もう一度お試しください。インストール済みの内容は保持されます。",
    [PW_TEXT_ERR_WRITE] = "PS5 のストレージへの書き込みが拒否されました。もう一度お試しください。繰り返し起こる場合は PS5 を再起動してください。",
    [PW_TEXT_ERR_CANCELLED] = "インストールを停止しました。インストール済みの内容は保持されます。",
    [PW_TEXT_ERR_BUILD] = "この MihawkOS では Windows 実行環境をインストールできません。MihawkOS を入れ直してください。",
    [PW_TEXT_INSTALLING_TITLE] = "Windows 実行環境をインストール中",
    [PW_TEXT_INSTALLING_BODY] = "ドライブは接続したままにしてください。約 1 分かかります。",
    [PW_TEXT_INSTALLING_CHECKING] = "インストール済みの内容を確認しています…",
    [PW_TEXT_INSTALLING_STATS] = "{1} / {2} MB · {3} MB/s",
    [PW_TEXT_INSTALLED] = "インストールしました。",
    [PW_TEXT_STOP] = "停止",
    [PW_TEXT_PREPARE_TITLE] = "Mihawk OS を準備しています",
    [PW_TEXT_PREPARE_BODY] = "初回の起動で Windows ドライブを作成します。数分かかり、しばらく画面が止まって見えることがあります。完了すると自動で終了し、この画面に戻ります。",
    [PW_TEXT_PREPARE_NOTE] = "初回の起動中はゲームでコントローラーが使えないため、ドライブの準備ができしだい MihawkOS が起動を終了します。",
    [PW_TEXT_PREPARE_START] = "起動",
    [PW_TEXT_PREPARE_FAILED] = "初回の起動が完了しませんでした。もう一度お試しください。中断したところから再開します。",
    [PW_TEXT_BUILD_TITLE] = "Windows ドライブを作成しています",
    [PW_TEXT_BUILD_BODY] = "約 1 分かかります。準備ができると、MihawkOS が自動でセットアップに戻ります。",
    [PW_TEXT_BUILD_SETTINGS] = "Windows を設定しています",
    [PW_TEXT_BUILD_DOTNET] = ".NET サポートをインストールしています",
    [PW_TEXT_BUILD_FINISHING] = "仕上げています",
    [PW_TEXT_DONE_TITLE] = "準備完了",
    [PW_TEXT_DONE_BODY] = "Mihawk OS の準備ができました。このセットアップは、ライブラリの Settings からもう一度実行できます。",
    [PW_TEXT_ENTER] = "Mihawk OS に入る",
    [PW_TEXT_HINT_SELECT] = "決定",
    [PW_TEXT_HINT_BACK] = "戻る",
    [PW_TEXT_HINT_MOVE] = "移動",
    [PW_TEXT_STEP_OF] = "ステップ {1}/{2}",
    [PW_TEXT_GIGABYTES] = "{1} GB",
    [PW_TEXT_DECIMAL] = ".",
    [PW_TEXT_STEAM_TITLE] = "Steam をセットアップ",
    [PW_TEXT_STEAM_BODY] = "Steam は PC ゲーム用の Valve のアプリです。初回は次の 4 つを行います：",
    [PW_TEXT_STEAM_STEP_DOWNLOAD] = "ダウンロード",
    [PW_TEXT_STEAM_STEP_DOWNLOAD_BODY] = "Steam のインストーラーを Valve のサーバーから直接取得します。",
    [PW_TEXT_STEAM_STEP_INSTALL] = "インストール",
    [PW_TEXT_STEAM_STEP_INSTALL_BODY] = "MihawkOS が Windows ドライブにインストールします。1 分かかりません。",
    [PW_TEXT_STEAM_STEP_UPDATE] = "初回起動",
    [PW_TEXT_STEAM_STEP_UPDATE_BODY] = "Steam が自動でアップデートします。約 5 分かかります。",
    [PW_TEXT_STEAM_STEP_SIGNIN] = "サインイン",
    [PW_TEXT_STEAM_STEP_SIGNIN_BODY] = "スマートフォンの Steam アプリで QR コードを読み取ります。",
    [PW_TEXT_STEAM_NOTE] = "インターネット接続が必要です。全体で約 10 分です。MihawkOS は Valve とは関係ありません。",
    [PW_TEXT_STEAM_START] = "開始",
    [PW_TEXT_STEAM_NOT_NOW] = "後で",
    [PW_TEXT_STEAM_DOWNLOADING] = "Steam をダウンロード中",
    [PW_TEXT_STEAM_DOWNLOADING_BODY] = "Valve のサーバーから取得しています。ダウンロードが終わるとすぐに MihawkOS が Steam "
        "をインストールします。画面が一瞬切り替わりますが、問題ありません。",
    [PW_TEXT_STEAM_MEGABYTES] = "{1} MB / 約 {2} MB",
    [PW_TEXT_STEAM_STARTING] = "インストールしました。Steam を起動しています…",
    [PW_TEXT_STEAM_FAIL_TITLE] = "Steam のセットアップが止まりました",
    [PW_TEXT_STEAM_ERR_OFFLINE] = "PS5 がインターネットに接続されていません。ネットワーク設定を確認してから、もう一度お試しください。",
    [PW_TEXT_STEAM_ERR_CLOCK] = "PS5 の日付または時刻が正しくないため、安全なダウンロードが拒否されました。PS5 の設定で日付と時刻を合わせてから、もう一度お試しください。",
    [PW_TEXT_STEAM_ERR_DOWNLOAD] = "Valve のサーバーから Steam のインストーラーをダウンロードできませんでした。しばらくしてから、もう一度お試しください。",
    [PW_TEXT_STEAM_ERR_INSTALL] = "Steam のインストーラーが完了しませんでした。もう一度お試しください。ダウンロードしたファイルは残っています。",
    [PW_TEXT_STEAM_ERR_STALLED] = "Steam のアップデートが長い間進まなかったため、Steam を終了しました。もう一度お試しください。止まったところから続きます。",
    [PW_TEXT_STEAM_ERR_CANCELLED] = "サインインがキャンセルされました。準備ができたら、もう一度お試しください。",
    [PW_TEXT_STEAM_ERR_TIMEOUT] = "30 分以内にサインインがなかったため、Steam を終了しました。準備ができたら、もう一度お試しください。",
    [PW_TEXT_STEAM_ERR_GUARD] = "Steam が Steam Guard のコードを求めました。もう一度試して、代わりに QR コードを使ってください。スマートフォンの Steam "
        "アプリで盾のアイコンをタップし、コードを読み取ります。",
    [PW_TEXT_STEAM_ERR_ENDED] = "サインインの前に Steam が終了しました。もう一度お試しください。",
    [PW_TEXT_STEAM_INSTALLING_TITLE] = "Steam をインストール中",
    [PW_TEXT_STEAM_INSTALLING_BODY] = "Valve のインストーラーが Steam を Windows ドライブに入れています。1 分かかりません。",
    [PW_TEXT_STEAM_UPDATING_TITLE] = "Steam をアップデート中",
    [PW_TEXT_STEAM_UPDATING_BODY] = "初回起動では Steam の最新版をダウンロードします。約 5 分かかり、しばらく画面が止まって見えることがありますが、問題ありません。",
    [PW_TEXT_STEAM_DOWNLOADED] = "{1} MB ダウンロード済み",
    [PW_TEXT_STEAM_ELAPSED] = "経過時間：{1}",
    [PW_TEXT_STEAM_SLOW] = "いつもより時間がかかっています。このまま待つか、もう一度お試しください。",
    [PW_TEXT_STEAM_SCAN_TITLE] = "スマートフォンをご用意ください",
    [PW_TEXT_STEAM_SCAN_BODY] = "Steam がサインインページを開いています。スマートフォンで Steam アプリを開き、盾のアイコンをタップして、画面の QR コードを読み取ってください。",
    [PW_TEXT_STEAM_CANCEL_NOTE] = "キャンセルするには、OPTIONS を 2 秒間押し続けます。",
    [PW_TEXT_STEAM_SIGNED_IN] = "サインインしました",
    [PW_TEXT_STEAM_SIGNED_IN_AS] = "{1} としてサインインしました",
    [PW_TEXT_STEAM_REOPEN_BODY] = "Steam をいったん終了し、Big Picture モードで開き直します。コントローラー向けに作られ、PS5 に合わせて設定を調整してあります。今後、Steam "
        "は最初から Big Picture で開きます。",
    [PW_TEXT_STEAM_DONE_BODY] = "今後、Steam は最初から Big Picture で開きます。コントローラー向けに作られ、PS5 "
        "に合わせて設定を調整してあります。もう一度サインインする必要はありません。",
    [PW_TEXT_STEAM_OPENING_IN] = "{1} 秒後に開きます",
};

static const char *const text_pt[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "Boas-vindas ao MihawkOS",
    [PW_TEXT_WELCOME_BODY] = "Vamos preparar seu PS5 para jogos do Windows. Leva cerca de dez minutos.",
    [PW_TEXT_GET_STARTED] = "Começar",
    [PW_TEXT_SKIP_SETUP] = "Pular configuração",
    [PW_TEXT_LANGUAGE_TITLE] = "Escolha seu idioma",
    [PW_TEXT_LANGUAGE_BODY] = "Seus jogos do Windows também vão usá-lo, com o layout de teclado dele. A área de trabalho "
        "do Mihawk OS continua em inglês.",
    [PW_TEXT_METHOD_TITLE] = "Escolha como instalar",
    [PW_TEXT_METHOD_BODY] = "O MihawkOS precisa do ambiente Windows dele, com cerca de 1,2 GB.",
    [PW_TEXT_METHOD_USB] = "Pen drive",
    [PW_TEXT_METHOD_USB_BODY] = "Copie os arquivos para um pen drive em qualquer computador.",
    [PW_TEXT_METHOD_FTP] = "FTP",
    [PW_TEXT_METHOD_FTP_BODY] = "Copie os arquivos para o seu PS5 pela sua rede.",
    [PW_TEXT_METHOD_INTERNET] = "Internet",
    [PW_TEXT_METHOD_INTERNET_BODY] = "Baixe direto no seu PS5.",
    [PW_TEXT_COMING_SOON] = "Em breve",
    [PW_TEXT_METHOD_INSTALLED] = "O ambiente Windows já está instalado.",
    [PW_TEXT_CONTINUE] = "Continuar",
    [PW_TEXT_QR_CAPTION] = "Escaneie para ver as instruções passo a passo no celular.",
    [PW_TEXT_USB_TITLE] = "Prepare um pen drive",
    [PW_TEXT_USB_STEP1] = "Em um computador, formate um pen drive em exFAT com tabela de partição MBR. Isso apaga o pen "
        "drive.",
    [PW_TEXT_USB_STEP2] = "Baixe o MihawkOS e copie a pasta mihawkos dele para a raiz do pen drive.",
    [PW_TEXT_USB_STEP3] = "Conecte o pen drive a uma porta USB do seu PS5.",
    [PW_TEXT_USB_STEP4] = "Escolha Verificar pen drive.",
    [PW_TEXT_CHECK_DRIVE] = "Verificar pen drive",
    [PW_TEXT_FOUND] = "O pen drive tem {1} para instalar.",
    [PW_TEXT_INSTALL] = "Instalar",
    [PW_TEXT_RETRY] = "Tentar de novo",
    [PW_TEXT_FTP_TITLE] = "Copie os arquivos por FTP",
    [PW_TEXT_FTP_STEP1] = "No seu PS5, inicie um servidor FTP como o payload ftpsrv (muitas vezes porta 2121).",
    [PW_TEXT_FTP_STEP2] = "Em um computador, descompacte o arquivo usb-mihawkos do MihawkOS.",
    [PW_TEXT_FTP_STEP3] = "Com um cliente FTP como o FileZilla, copie os arquivos da pasta mihawkos para "
        "/data/ps5proton/install.",
    [PW_TEXT_FTP_STEP4] = "Escolha Verificar arquivos.",
    [PW_TEXT_CHECK_FILES] = "Verificar arquivos",
    [PW_TEXT_FTP_FOUND] = "A pasta de instalação tem {1} para instalar.",
    [PW_TEXT_ERR_FTP_EMPTY] = "Não há arquivos do ambiente em /data/ps5proton/install. Copie para lá os arquivos da "
        "pasta mihawkos, não a pasta em si, e tente de novo.",
    [PW_TEXT_ERR_FTP_PARTIAL] = "/data/ps5proton/install tem {1} de {2} partes. Copie os arquivos que faltam e tente "
        "de novo.",
    [PW_TEXT_FTP_INSTALLING_BODY] = "Os arquivos são lidos de /data/ps5proton/install. Leva cerca de um minuto.",
    [PW_TEXT_ERR_FTP_READ] = "Não foi possível ler um arquivo de /data/ps5proton/install. Copie os arquivos de novo e "
        "tente outra vez. O que já foi instalado é mantido.",
    [PW_TEXT_ERR_FTP_DAMAGED] = "Um arquivo de /data/ps5proton/install está danificado ou é de outra versão. Copie os "
        "arquivos da pasta mihawkos de novo e tente outra vez.",
    [PW_TEXT_ERR_NO_STICK] = "Nenhum pen drive encontrado. Conecte-o, espere alguns segundos e tente de novo. Se ele já "
        "estiver conectado, formate-o em exFAT com MBR: o PS5 não lê unidades GPT nem NTFS.",
    [PW_TEXT_ERR_NO_FOLDER] = "O pen drive não tem uma pasta mihawkos na raiz. Copie a pasta para o pen drive e tente de "
        "novo.",
    [PW_TEXT_ERR_PARTIAL] = "A pasta mihawkos está incompleta: tem {1} de {2} partes. Copie a pasta inteira de novo e tente "
        "outra vez.",
    [PW_TEXT_ERR_NO_SPACE] = "Seu PS5 precisa de {1} livres e tem {2}. Apague algo e tente de novo.",
    [PW_TEXT_ERR_READ] = "Não foi possível ler o pen drive. Verifique se ele ainda está conectado e tente de novo. O que "
        "já foi instalado é mantido.",
    [PW_TEXT_ERR_DAMAGED] = "Um arquivo do pen drive está danificado ou é de outra versão. Copie a pasta mihawkos de novo "
        "e tente outra vez.",
    [PW_TEXT_ERR_FULL] = "O armazenamento do seu PS5 está cheio. Apague algo e tente de novo. O que já foi instalado é "
        "mantido.",
    [PW_TEXT_ERR_WRITE] = "O armazenamento do seu PS5 recusou uma gravação. Tente de novo; se acontecer outra vez, reinicie "
        "o PS5.",
    [PW_TEXT_ERR_CANCELLED] = "A instalação parou. O que já foi instalado é mantido.",
    [PW_TEXT_ERR_BUILD] = "Esta cópia do MihawkOS não pode instalar um ambiente Windows. Reinstale o MihawkOS.",
    [PW_TEXT_INSTALLING_TITLE] = "Instalando o ambiente Windows",
    [PW_TEXT_INSTALLING_BODY] = "Deixe o pen drive conectado. Leva cerca de um minuto.",
    [PW_TEXT_INSTALLING_CHECKING] = "Verificando o que já está instalado…",
    [PW_TEXT_INSTALLING_STATS] = "{1} de {2} MB · {3} MB/s",
    [PW_TEXT_INSTALLED] = "Instalado.",
    [PW_TEXT_STOP] = "Parar",
    [PW_TEXT_PREPARE_TITLE] = "Preparando o Mihawk OS",
    [PW_TEXT_PREPARE_BODY] = "A primeira inicialização cria seu disco do Windows. Leva alguns minutos, e a tela pode ficar "
        "parada por um tempo. Ela termina sozinha e volta para cá.",
    [PW_TEXT_PREPARE_NOTE] = "Durante esta primeira inicialização o controle não funciona nos jogos, por isso o MihawkOS "
        "a encerra assim que o disco fica pronto.",
    [PW_TEXT_PREPARE_START] = "Iniciar",
    [PW_TEXT_PREPARE_FAILED] = "A primeira inicialização não terminou. Tente de novo: ela continua de onde parou.",
    [PW_TEXT_BUILD_TITLE] = "Criando seu disco do Windows",
    [PW_TEXT_BUILD_BODY] = "Leva cerca de um minuto. Quando o disco estiver pronto, o MihawkOS volta sozinho para a "
        "configuração.",
    [PW_TEXT_BUILD_SETTINGS] = "Configurando o Windows",
    [PW_TEXT_BUILD_DOTNET] = "Instalando o suporte a .NET",
    [PW_TEXT_BUILD_FINISHING] = "Finalizando",
    [PW_TEXT_DONE_TITLE] = "Tudo pronto",
    [PW_TEXT_DONE_BODY] = "O Mihawk OS está pronto. Você pode refazer esta configuração em Settings, na biblioteca.",
    [PW_TEXT_ENTER] = "Entrar no Mihawk OS",
    [PW_TEXT_HINT_SELECT] = "Selecionar",
    [PW_TEXT_HINT_BACK] = "Voltar",
    [PW_TEXT_HINT_MOVE] = "Mover",
    [PW_TEXT_STEP_OF] = "Etapa {1} de {2}",
    [PW_TEXT_GIGABYTES] = "{1} GB",
    [PW_TEXT_DECIMAL] = ",",
    [PW_TEXT_STEAM_TITLE] = "Configurar o Steam",
    [PW_TEXT_STEAM_BODY] = "O Steam é o app da Valve para seus jogos de PC. Na primeira vez, acontecem quatro coisas:",
    [PW_TEXT_STEAM_STEP_DOWNLOAD] = "Download",
    [PW_TEXT_STEAM_STEP_DOWNLOAD_BODY] = "O instalador do Steam vem direto dos servidores da Valve.",
    [PW_TEXT_STEAM_STEP_INSTALL] = "Instalação",
    [PW_TEXT_STEAM_STEP_INSTALL_BODY] = "O MihawkOS o instala no seu disco do Windows em menos de um minuto.",
    [PW_TEXT_STEAM_STEP_UPDATE] = "Primeira abertura",
    [PW_TEXT_STEAM_STEP_UPDATE_BODY] = "O Steam se atualiza sozinho. Leva cerca de 5 minutos.",
    [PW_TEXT_STEAM_STEP_SIGNIN] = "Login",
    [PW_TEXT_STEAM_STEP_SIGNIN_BODY] = "Leia um QR code com o app do Steam no seu celular.",
    [PW_TEXT_STEAM_NOTE] = "É preciso estar conectado à internet. Cerca de 10 minutos no total. O MihawkOS não é "
        "afiliado à Valve.",
    [PW_TEXT_STEAM_START] = "Começar",
    [PW_TEXT_STEAM_NOT_NOW] = "Agora não",
    [PW_TEXT_STEAM_DOWNLOADING] = "Baixando o Steam",
    [PW_TEXT_STEAM_DOWNLOADING_BODY] = "Dos servidores da Valve. O MihawkOS instala o Steam assim que o download "
        "termina; a tela muda por um instante, o que é normal.",
    [PW_TEXT_STEAM_MEGABYTES] = "{1} de cerca de {2} MB",
    [PW_TEXT_STEAM_STARTING] = "Instalado. Abrindo o Steam…",
    [PW_TEXT_STEAM_FAIL_TITLE] = "A configuração do Steam parou",
    [PW_TEXT_STEAM_ERR_OFFLINE] = "Seu PS5 não está conectado à internet. Verifique as configurações de rede e tente "
        "de novo.",
    [PW_TEXT_STEAM_ERR_CLOCK] = "A data ou a hora do seu PS5 está errada, por isso o download seguro foi recusado. "
        "Ajuste a data e a hora nas configurações do PS5 e tente de novo.",
    [PW_TEXT_STEAM_ERR_DOWNLOAD] = "Não foi possível baixar o instalador do Steam dos servidores da Valve. Tente de "
        "novo daqui a pouco.",
    [PW_TEXT_STEAM_ERR_INSTALL] = "O instalador do Steam não terminou. Tente de novo: o download foi mantido.",
    [PW_TEXT_STEAM_ERR_STALLED] = "A atualização do Steam ficou parada por muito tempo, então o Steam foi fechado. "
        "Tente de novo: ela continua de onde parou.",
    [PW_TEXT_STEAM_ERR_CANCELLED] = "O login foi cancelado. Tente de novo quando quiser.",
    [PW_TEXT_STEAM_ERR_TIMEOUT] = "Nenhum login foi feito em 30 minutos, então o Steam foi fechado. Tente de novo "
        "quando quiser.",
    [PW_TEXT_STEAM_ERR_GUARD] = "O Steam pediu um código do Steam Guard. Tente de novo e use o QR code: no app do "
        "Steam no seu celular, toque no escudo e leia o código.",
    [PW_TEXT_STEAM_ERR_ENDED] = "O Steam fechou antes do login. Tente de novo.",
    [PW_TEXT_STEAM_INSTALLING_TITLE] = "Instalando o Steam",
    [PW_TEXT_STEAM_INSTALLING_BODY] = "O instalador da Valve está colocando o Steam no seu disco do Windows. Leva "
        "menos de um minuto.",
    [PW_TEXT_STEAM_UPDATING_TITLE] = "O Steam está se atualizando",
    [PW_TEXT_STEAM_UPDATING_BODY] = "A primeira abertura baixa a versão mais recente do Steam. Leva cerca de 5 "
        "minutos, e a tela pode ficar parada por um tempo: é normal.",
    [PW_TEXT_STEAM_DOWNLOADED] = "{1} MB baixados",
    [PW_TEXT_STEAM_ELAPSED] = "Tempo decorrido: {1}",
    [PW_TEXT_STEAM_SLOW] = "Está demorando mais que o normal. Você pode continuar esperando ou tentar de novo.",
    [PW_TEXT_STEAM_SCAN_TITLE] = "Deixe o celular à mão",
    [PW_TEXT_STEAM_SCAN_BODY] = "O Steam está abrindo a página de login. Abra o app do Steam no celular, toque no "
        "escudo e leia o QR code na tela.",
    [PW_TEXT_STEAM_CANCEL_NOTE] = "Para cancelar, segure OPTIONS por 2 segundos.",
    [PW_TEXT_STEAM_SIGNED_IN] = "Login feito",
    [PW_TEXT_STEAM_SIGNED_IN_AS] = "Login feito como {1}",
    [PW_TEXT_STEAM_REOPEN_BODY] = "O Steam fecha agora e reabre no Big Picture: feito para o controle, com "
        "configurações ajustadas para o PS5. De agora em diante, o Steam abre direto no Big Picture.",
    [PW_TEXT_STEAM_DONE_BODY] = "De agora em diante, o Steam abre direto no Big Picture: feito para o controle, com "
        "configurações ajustadas para o PS5. Você não vai precisar fazer login de novo.",
    [PW_TEXT_STEAM_OPENING_IN] = "Abrindo em {1} s",
};

/* The languages, in the owner's order (2026-10-18). Chinese and Japanese draw with their own faces (the setup's
 * Noto Sans SC and JP, baked with only these strings' characters: ps5/tools/generate_assets.py); the others with the
 * launcher's. Each flag is in ps5/assets/flags. */
/* Polski. */
static const char *const text_pl[PW_TEXT_COUNT] = {
    [PW_TEXT_WELCOME_TITLE] = "Witaj w MihawkOS",
    [PW_TEXT_WELCOME_BODY] = "Przygotujmy Twoją PS5 do gier z Windows. Zajmie to około dziesięciu minut.",
    [PW_TEXT_GET_STARTED] = "Zaczynamy",
    [PW_TEXT_SKIP_SETUP] = "Pomiń konfigurację",
    [PW_TEXT_LANGUAGE_TITLE] = "Wybierz język",
    [PW_TEXT_LANGUAGE_BODY] = "Twoje gry z Windows też będą go używać, razem z układem klawiatury. Pulpit Mihawk OS "
        "pozostaje po angielsku.",
    [PW_TEXT_METHOD_TITLE] = "Wybierz sposób instalacji",
    [PW_TEXT_METHOD_BODY] = "MihawkOS potrzebuje swojego środowiska Windows, około 1,2 GB.",
    [PW_TEXT_METHOD_USB] = "Pamięć USB",
    [PW_TEXT_METHOD_USB_BODY] = "Skopiuj pliki na pamięć USB na dowolnym komputerze.",
    [PW_TEXT_METHOD_FTP] = "FTP",
    [PW_TEXT_METHOD_FTP_BODY] = "Skopiuj pliki na PS5 przez swoją sieć.",
    [PW_TEXT_METHOD_INTERNET] = "Internet",
    [PW_TEXT_METHOD_INTERNET_BODY] = "Pobierz prosto na swoją PS5.",
    [PW_TEXT_COMING_SOON] = "Wkrótce",
    [PW_TEXT_METHOD_INSTALLED] = "Środowisko Windows jest już zainstalowane.",
    [PW_TEXT_CONTINUE] = "Dalej",
    [PW_TEXT_QR_CAPTION] = "Zeskanuj kod, aby zobaczyć instrukcję krok po kroku na telefonie.",
    [PW_TEXT_USB_TITLE] = "Przygotuj pamięć USB",
    [PW_TEXT_USB_STEP1] = "Na komputerze sformatuj pamięć USB w systemie exFAT z tablicą partycji MBR. Spowoduje to "
        "wymazanie pamięci.",
    [PW_TEXT_USB_STEP2] = "Pobierz MihawkOS i skopiuj jego folder mihawkos do głównego katalogu pamięci.",
    [PW_TEXT_USB_STEP3] = "Podłącz pamięć do portu USB w PS5.",
    [PW_TEXT_USB_STEP4] = "Wybierz Sprawdź pamięć.",
    [PW_TEXT_CHECK_DRIVE] = "Sprawdź pamięć",
    [PW_TEXT_FOUND] = "Pamięć zawiera {1} do zainstalowania.",
    [PW_TEXT_INSTALL] = "Zainstaluj",
    [PW_TEXT_RETRY] = "Spróbuj ponownie",
    [PW_TEXT_FTP_TITLE] = "Skopiuj pliki przez FTP",
    [PW_TEXT_FTP_STEP1] = "Na PS5 uruchom serwer FTP, na przykład payload ftpsrv (często port 2121).",
    [PW_TEXT_FTP_STEP2] = "Na komputerze rozpakuj plik usb-mihawkos z MihawkOS.",
    [PW_TEXT_FTP_STEP3] = "W kliencie FTP, takim jak FileZilla, skopiuj pliki z folderu mihawkos do "
        "/data/ps5proton/install.",
    [PW_TEXT_FTP_STEP4] = "Wybierz Sprawdź pliki.",
    [PW_TEXT_CHECK_FILES] = "Sprawdź pliki",
    [PW_TEXT_FTP_FOUND] = "Folder instalacji zawiera {1} do zainstalowania.",
    [PW_TEXT_ERR_FTP_EMPTY] = "W /data/ps5proton/install nie ma plików środowiska. Skopiuj tam pliki z folderu "
        "mihawkos, a nie sam folder, i spróbuj ponownie.",
    [PW_TEXT_ERR_FTP_PARTIAL] = "/data/ps5proton/install zawiera {1} z {2} części. Skopiuj brakujące pliki i spróbuj "
        "ponownie.",
    [PW_TEXT_FTP_INSTALLING_BODY] = "Pliki są odczytywane z /data/ps5proton/install. Zajmuje to około minuty.",
    [PW_TEXT_ERR_FTP_READ] = "Nie można odczytać pliku w /data/ps5proton/install. Skopiuj tam pliki jeszcze raz i "
        "spróbuj ponownie. To, co już zainstalowano, zostaje.",
    [PW_TEXT_ERR_FTP_DAMAGED] = "Plik w /data/ps5proton/install jest uszkodzony lub pochodzi z innej wersji. Skopiuj "
        "jeszcze raz pliki z folderu mihawkos i spróbuj ponownie.",
    [PW_TEXT_ERR_NO_STICK] = "Nie znaleziono pamięci USB. Podłącz ją, odczekaj kilka sekund i spróbuj ponownie. Jeśli "
        "jest już podłączona, sformatuj ją w exFAT z MBR: PS5 nie odczytuje dysków GPT ani NTFS.",
    [PW_TEXT_ERR_NO_FOLDER] = "W głównym katalogu pamięci nie ma folderu mihawkos. Skopiuj folder na pamięć i spróbuj "
        "ponownie.",
    [PW_TEXT_ERR_PARTIAL] = "Folder mihawkos jest niekompletny: zawiera {1} z {2} części. Skopiuj jeszcze raz cały "
        "folder i spróbuj ponownie.",
    [PW_TEXT_ERR_NO_SPACE] = "Twoja PS5 potrzebuje {1} wolnego miejsca, a ma {2}. Usuń coś i spróbuj ponownie.",
    [PW_TEXT_ERR_READ] = "Nie można odczytać pamięci USB. Sprawdź, czy nadal jest podłączona, i spróbuj ponownie. To, "
        "co już zainstalowano, zostaje.",
    [PW_TEXT_ERR_DAMAGED] = "Plik na pamięci jest uszkodzony lub pochodzi z innej wersji. Skopiuj jeszcze raz folder "
        "mihawkos i spróbuj ponownie.",
    [PW_TEXT_ERR_FULL] = "Pamięć masowa Twojej PS5 jest pełna. Usuń coś i spróbuj ponownie. To, co już zainstalowano, "
        "zostaje.",
    [PW_TEXT_ERR_WRITE] = "Pamięć masowa Twojej PS5 odrzuciła zapis. Spróbuj ponownie; jeśli to się powtórzy, uruchom "
        "PS5 ponownie.",
    [PW_TEXT_ERR_CANCELLED] = "Instalacja została zatrzymana. To, co już zainstalowano, zostaje.",
    [PW_TEXT_ERR_BUILD] = "Ta kopia MihawkOS nie może zainstalować środowiska Windows. Zainstaluj MihawkOS ponownie.",
    [PW_TEXT_INSTALLING_TITLE] = "Instalowanie środowiska Windows",
    [PW_TEXT_INSTALLING_BODY] = "Nie odłączaj pamięci. Zajmuje to około minuty.",
    [PW_TEXT_INSTALLING_CHECKING] = "Sprawdzanie, co jest już zainstalowane…",
    [PW_TEXT_INSTALLING_STATS] = "{1} z {2} MB · {3} MB/s",
    [PW_TEXT_INSTALLED] = "Zainstalowano.",
    [PW_TEXT_STOP] = "Zatrzymaj",
    [PW_TEXT_PREPARE_TITLE] = "Przygotowywanie Mihawk OS",
    [PW_TEXT_PREPARE_BODY] = "Pierwsze uruchomienie tworzy Twój dysk Windows. Trwa to kilka minut, a obraz może się "
        "przez chwilę nie zmieniać. Kończy się samo i wraca tutaj.",
    [PW_TEXT_PREPARE_NOTE] = "Podczas tego pierwszego uruchomienia kontroler nie działa w grach, dlatego MihawkOS "
        "kończy je, gdy tylko dysk jest gotowy.",
    [PW_TEXT_PREPARE_START] = "Uruchom",
    [PW_TEXT_PREPARE_FAILED] = "Pierwsze uruchomienie się nie zakończyło. Spróbuj ponownie: będzie kontynuowane od "
        "miejsca, w którym się zatrzymało.",
    [PW_TEXT_BUILD_TITLE] = "Tworzenie dysku Windows",
    [PW_TEXT_BUILD_BODY] = "Zajmuje to około minuty. Gdy dysk będzie gotowy, MihawkOS sam wróci do konfiguracji.",
    [PW_TEXT_BUILD_SETTINGS] = "Konfigurowanie Windows",
    [PW_TEXT_BUILD_DOTNET] = "Instalowanie obsługi .NET",
    [PW_TEXT_BUILD_FINISHING] = "Kończenie",
    [PW_TEXT_DONE_TITLE] = "Wszystko gotowe",
    [PW_TEXT_DONE_BODY] = "Mihawk OS jest gotowy. Tę konfigurację możesz powtórzyć w Settings, w bibliotece.",
    [PW_TEXT_ENTER] = "Otwórz Mihawk OS",
    [PW_TEXT_HINT_SELECT] = "Wybierz",
    [PW_TEXT_HINT_BACK] = "Wstecz",
    [PW_TEXT_HINT_MOVE] = "Przesuń",
    [PW_TEXT_STEP_OF] = "Krok {1} z {2}",
    [PW_TEXT_GIGABYTES] = "{1} GB",
    [PW_TEXT_DECIMAL] = ",",
    [PW_TEXT_STEAM_TITLE] = "Skonfiguruj Steam",
    [PW_TEXT_STEAM_BODY] = "Steam to aplikacja Valve do Twoich gier na PC. Za pierwszym razem dzieją się cztery "
        "rzeczy:",
    [PW_TEXT_STEAM_STEP_DOWNLOAD] = "Pobieranie",
    [PW_TEXT_STEAM_STEP_DOWNLOAD_BODY] = "Instalator Steam pochodzi prosto z serwerów Valve.",
    [PW_TEXT_STEAM_STEP_INSTALL] = "Instalacja",
    [PW_TEXT_STEAM_STEP_INSTALL_BODY] = "MihawkOS instaluje go na Twoim dysku Windows w niecałą minutę.",
    [PW_TEXT_STEAM_STEP_UPDATE] = "Pierwsze uruchomienie",
    [PW_TEXT_STEAM_STEP_UPDATE_BODY] = "Steam sam się aktualizuje. Trwa to około 5 minut.",
    [PW_TEXT_STEAM_STEP_SIGNIN] = "Logowanie",
    [PW_TEXT_STEAM_STEP_SIGNIN_BODY] = "Zeskanuj kod QR aplikacją Steam na telefonie.",
    [PW_TEXT_STEAM_NOTE] = "Potrzebne jest połączenie z internetem. W sumie około 10 minut. MihawkOS nie jest "
        "powiązany z Valve.",
    [PW_TEXT_STEAM_START] = "Rozpocznij",
    [PW_TEXT_STEAM_NOT_NOW] = "Nie teraz",
    [PW_TEXT_STEAM_DOWNLOADING] = "Pobieranie Steam",
    [PW_TEXT_STEAM_DOWNLOADING_BODY] = "Z serwerów Valve. MihawkOS zainstaluje Steam, gdy tylko pobieranie się "
        "skończy; ekran na chwilę się zmieni, to normalne.",
    [PW_TEXT_STEAM_MEGABYTES] = "{1} z około {2} MB",
    [PW_TEXT_STEAM_STARTING] = "Zainstalowano. Uruchamianie Steam…",
    [PW_TEXT_STEAM_FAIL_TITLE] = "Konfiguracja Steam się zatrzymała",
    [PW_TEXT_STEAM_ERR_OFFLINE] = "Twoja PS5 nie jest połączona z internetem. Sprawdź jej ustawienia sieci i spróbuj "
        "ponownie.",
    [PW_TEXT_STEAM_ERR_CLOCK] = "Data lub godzina na Twojej PS5 jest nieprawidłowa, więc bezpieczne pobieranie "
        "zostało odrzucone. Ustaw datę i godzinę w ustawieniach PS5 i spróbuj ponownie.",
    [PW_TEXT_STEAM_ERR_DOWNLOAD] = "Nie udało się pobrać instalatora Steam z serwerów Valve. Spróbuj ponownie za "
        "chwilę.",
    [PW_TEXT_STEAM_ERR_INSTALL] = "Instalator Steam nie zakończył pracy. Spróbuj ponownie: pobrany plik zostaje.",
    [PW_TEXT_STEAM_ERR_STALLED] = "Aktualizacja Steam długo nie postępowała, więc Steam został zamknięty. Spróbuj "
        "ponownie: aktualizacja będzie kontynuowana od miejsca, w którym się zatrzymała.",
    [PW_TEXT_STEAM_ERR_CANCELLED] = "Logowanie zostało anulowane. Spróbuj ponownie, kiedy zechcesz.",
    [PW_TEXT_STEAM_ERR_TIMEOUT] = "Przez 30 minut nie nastąpiło logowanie, więc Steam został zamknięty. Spróbuj "
        "ponownie, kiedy zechcesz.",
    [PW_TEXT_STEAM_ERR_GUARD] = "Steam poprosił o kod Steam Guard. Spróbuj ponownie i zamiast tego użyj kodu QR: w "
        "aplikacji Steam na telefonie dotknij tarczy, a potem zeskanuj kod.",
    [PW_TEXT_STEAM_ERR_ENDED] = "Steam zamknął się przed zalogowaniem. Spróbuj ponownie.",
    [PW_TEXT_STEAM_INSTALLING_TITLE] = "Instalowanie Steam",
    [PW_TEXT_STEAM_INSTALLING_BODY] = "Instalator Valve umieszcza Steam na Twoim dysku Windows. Trwa to niecałą "
        "minutę.",
    [PW_TEXT_STEAM_UPDATING_TITLE] = "Steam się aktualizuje",
    [PW_TEXT_STEAM_UPDATING_BODY] = "Przy pierwszym uruchomieniu Steam pobiera swoją najnowszą wersję. Trwa to około "
        "5 minut, a obraz może się przez chwilę nie zmieniać: to normalne.",
    [PW_TEXT_STEAM_DOWNLOADED] = "Pobrano {1} MB",
    [PW_TEXT_STEAM_ELAPSED] = "Upłynęło: {1}",
    [PW_TEXT_STEAM_SLOW] = "Trwa to dłużej niż zwykle. Możesz dalej czekać albo spróbować ponownie.",
    [PW_TEXT_STEAM_SCAN_TITLE] = "Przygotuj telefon",
    [PW_TEXT_STEAM_SCAN_BODY] = "Steam otwiera stronę logowania. Otwórz aplikację Steam na telefonie, dotknij tarczy, "
        "a potem zeskanuj kod QR na ekranie.",
    [PW_TEXT_STEAM_CANCEL_NOTE] = "Aby anulować, przytrzymaj OPTIONS przez 2 sekundy.",
    [PW_TEXT_STEAM_SIGNED_IN] = "Zalogowano",
    [PW_TEXT_STEAM_SIGNED_IN_AS] = "Zalogowano jako {1}",
    [PW_TEXT_STEAM_REOPEN_BODY] = "Steam zamknie się teraz i otworzy ponownie w trybie Big Picture: stworzonym do gry "
        "na kontrolerze, z ustawieniami dobranymi do PS5. Od teraz Steam będzie otwierać się od razu w Big Picture.",
    [PW_TEXT_STEAM_DONE_BODY] = "Od teraz Steam otwiera się od razu w trybie Big Picture: stworzonym do gry na "
        "kontrolerze, z ustawieniami dobranymi do PS5. Nie musisz logować się ponownie.",
    [PW_TEXT_STEAM_OPENING_IN] = "Otwarcie za {1} s",
};

static const PwSetupLanguage languages[] = {
    {"en-US", "English (US)", "us", "en_US.UTF-8", NULL, text_en},
    {"sv-SE", "Svenska", "se", "sv_SE.UTF-8", NULL, text_sv},
    {"fr-FR", "Français", "fr", "fr_FR.UTF-8", NULL, text_fr},
    {"es-ES", "Español", "es", "es_ES.UTF-8", NULL, text_es},
    {"de-DE", "Deutsch", "de", "de_DE.UTF-8", NULL, text_de},
    {"it-IT", "Italiano", "it", "it_IT.UTF-8", NULL, text_it},
    {"ru-RU", "Русский", "ru", "ru_RU.UTF-8", NULL, text_ru},
    {"zh-CN", "简体中文", "cn", "zh_CN.UTF-8", "noto-sans-sc", text_zh},
    {"ja-JP", "日本語", "jp", "ja_JP.UTF-8", "noto-sans-jp", text_ja},
    {"pt-BR", "Português (Brasil)", "br", "pt_BR.UTF-8", NULL, text_pt},
    {"pl-PL", "Polski", "pl", "pl_PL.UTF-8", NULL, text_pl},
};

const PwSetupLanguage *pw_setup_languages(size_t *count)
{
    *count = sizeof(languages) / sizeof(languages[0]);
    return languages;
}

const PwSetupLanguage *pw_setup_language(const char *tag)
{
    for (size_t i = 0; tag && i < sizeof(languages) / sizeof(languages[0]); ++i)
        if (languages[i].text && !strcmp(languages[i].tag, tag)) return &languages[i];
    return NULL;
}

const PwSetupLanguage *pw_setup_default_language(void)
{
    return &languages[0];
}

const char *pw_setup_text(const PwSetupLanguage *language, PwSetupText key)
{
    if ((unsigned)key >= PW_TEXT_COUNT) return "";
    if (!language || !language->text) language = pw_setup_default_language();
    return language->text[key];
}

PwSetupText pw_setup_route_text(int ftp, PwSetupText key)
{
    if (!ftp) return key;
    switch (key) {
    case PW_TEXT_USB_TITLE: return PW_TEXT_FTP_TITLE;
    case PW_TEXT_USB_STEP1: return PW_TEXT_FTP_STEP1;
    case PW_TEXT_USB_STEP2: return PW_TEXT_FTP_STEP2;
    case PW_TEXT_USB_STEP3: return PW_TEXT_FTP_STEP3;
    case PW_TEXT_USB_STEP4: return PW_TEXT_FTP_STEP4;
    case PW_TEXT_CHECK_DRIVE: return PW_TEXT_CHECK_FILES;
    case PW_TEXT_FOUND: return PW_TEXT_FTP_FOUND;
    case PW_TEXT_ERR_NO_STICK:
    case PW_TEXT_ERR_NO_FOLDER: return PW_TEXT_ERR_FTP_EMPTY;
    case PW_TEXT_ERR_PARTIAL: return PW_TEXT_ERR_FTP_PARTIAL;
    case PW_TEXT_ERR_READ: return PW_TEXT_ERR_FTP_READ;
    case PW_TEXT_ERR_DAMAGED: return PW_TEXT_ERR_FTP_DAMAGED;
    case PW_TEXT_INSTALLING_BODY: return PW_TEXT_FTP_INSTALLING_BODY;
    default: return key;
    }
}

size_t pw_setup_fill(char *out, size_t size, const char *text, const char *const *args, size_t count)
{
    size_t used = 0;
    if (!size) return 0;
    for (const char *p = text; *p; ++p) {
        const char *piece = p;
        size_t length = 1;
        if (p[0] == '{' && p[1] >= '1' && p[1] <= '9' && p[2] == '}' && (size_t)(p[1] - '1') < count) {
            piece = args[p[1] - '1'];
            length = strlen(piece);
            p += 2;
        }
        if (length > size - 1 - used) length = size - 1 - used;
        memcpy(out + used, piece, length);
        used += length;
    }
    out[used] = 0;
    return used;
}

size_t pw_setup_gigabytes(const PwSetupLanguage *language, unsigned long long bytes, char *out, size_t size)
{
    const unsigned long long hundredths = (bytes + 5000000ull) / 10000000ull;
    char number[48];
    snprintf(number, sizeof(number), "%llu%s%02llu", hundredths / 100, pw_setup_text(language, PW_TEXT_DECIMAL),
             hundredths % 100);
    const char *args[] = {number};
    return pw_setup_fill(out, size, pw_setup_text(language, PW_TEXT_GIGABYTES), args, 1);
}

int pw_setup_record_load(PwSetupRecord *out, const char *path)
{
    char text[512];
    memset(out, 0, sizeof(*out));
    FILE *file = fopen(path, "r");
    if (!file) return errno;
    const size_t size = fread(text, 1, sizeof(text), file);
    const int error = ferror(file) ? EIO : 0;
    fclose(file);
    if (error) return error;
    const char *end = text + size;
    for (const char *line = text; line < end;) {
        const char *stop = memchr(line, '\n', (size_t)(end - line));
        if (!stop) stop = end;
        size_t length = (size_t)(stop - line);
        if (length && line[length - 1] == '\r') --length;
        if (length > 9 && !memcmp(line, "language=", 9) && length - 9 < sizeof(out->language)) {
            memcpy(out->language, line + 9, length - 9);
            out->language[length - 9] = 0;
        } else if (length == 6 && !memcmp(line, "done=1", 6)) {
            out->done = 1;
        }
        line = stop + 1;
    }
    return 0;
}

int pw_setup_record_save(const PwSetupRecord *record, const char *path)
{
    char temporary[1024];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return ENAMETOOLONG;
    FILE *file = fopen(temporary, "w");
    if (!file) return errno;
    int error = 0;
    if ((record->language[0] && fprintf(file, "language=%s\n", record->language) < 0) ||
        (record->done && fputs("done=1\n", file) < 0))
        error = EIO;
    if (fflush(file) || fsync(fileno(file))) error = errno;
    if (fclose(file) && !error) error = errno;
    if (!error && rename(temporary, path)) error = errno;
    if (error) unlink(temporary);
    return error;
}

int pw_setup_prefix_made(char *stamp)
{
    size_t length = strlen(stamp);
    while (length && strchr("\r\n ", stamp[length - 1])) stamp[--length] = 0;
    return length >= 6 && !strcmp(stamp + length - 6, " wow64");
}

PwSetupBuild pw_setup_build_next(PwSetupBuild stage, const char *command_line)
{
    if (!command_line || stage >= PW_SETUP_BUILD_FINISHING) return stage;
    const int mono = strstr(command_line, "mono") != NULL;
    if (stage == PW_SETUP_BUILD_SETTINGS) return mono ? PW_SETUP_BUILD_DOTNET : stage;
    return mono ? stage : PW_SETUP_BUILD_FINISHING;
}

/* The cover's marks: anti-aliased by each pixel's distance to the shape, over the square they own. */
enum { MARK_X0 = PW_SETUP_COVER_MARK_X - 32, MARK_X1 = PW_SETUP_COVER_MARK_X + 32,
       MARK_Y0 = PW_SETUP_COVER_ROW_Y - 32, MARK_Y1 = PW_SETUP_COVER_ROW_Y + 2 * PW_SETUP_COVER_ROW_STEP + 32 };

static void cover_blend(unsigned *pixel, unsigned color, float coverage)
{
    if (coverage <= 0.0f) return;
    const unsigned alpha = coverage >= 1.0f ? 255u : (unsigned)(coverage * 255.0f + 0.5f), keep = 255u - alpha;
    unsigned out = 0xff000000u;
    for (int shift = 0; shift < 24; shift += 8)
        out |= ((((*pixel >> shift) & 0xffu) * keep + ((color >> shift) & 0xffu) * alpha + 127u) / 255u) << shift;
    *pixel = out;
}

/* shape: 0 a disc of radius r, 1 a ring of radius r and half-width w, 2 the segment (x0, y0)-(x1, y1) of half-width w. */
static void cover_shape(unsigned *frame, int shape, float x0, float y0, float x1, float y1, float r, float w,
                        unsigned color, float strength)
{
    const float reach = (shape == 2 ? 0.0f : r) + w + 1.0f;
    const int left = (int)fminf(x0, x1) - (int)reach, right = (int)fmaxf(x0, x1) + (int)reach + 1;
    const int top = (int)fminf(y0, y1) - (int)reach, bottom = (int)fmaxf(y0, y1) + (int)reach + 1;
    for (int y = top < MARK_Y0 ? MARK_Y0 : top; y < bottom && y < MARK_Y1; ++y)
        for (int x = left < MARK_X0 ? MARK_X0 : left; x < right && x < MARK_X1; ++x) {
            const float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float distance;
            if (shape == 2) {
                const float dx = x1 - x0, dy = y1 - y0;
                float t = ((px - x0) * dx + (py - y0) * dy) / (dx * dx + dy * dy);
                t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
                distance = hypotf(px - x0 - t * dx, py - y0 - t * dy) - w;
            } else {
                const float d = hypotf(px - x0, py - y0);
                distance = shape == 0 ? d - r : fabsf(d - r) - w;
            }
            cover_blend(&frame[(size_t)y * PW_SETUP_COVER_WIDTH + (size_t)x], color, (0.5f - distance) * strength);
        }
}

void pw_setup_cover_draw(unsigned *frame, const unsigned *picture, PwSetupBuild stage, unsigned tick)
{
    /* The tour's Gloss colours (examples/ps5proton/setup_tour.hpp's renders): success, primary, and a muted grey. */
    const unsigned green = 0xff32a56du, blue = 0xff2f8cf0u, grey = 0xff96a0acu, white = 0xffffffffu;
    for (int y = MARK_Y0; y < MARK_Y1; ++y)
        memcpy(frame + (size_t)y * PW_SETUP_COVER_WIDTH + MARK_X0, picture + (size_t)y * PW_SETUP_COVER_WIDTH + MARK_X0,
               (MARK_X1 - MARK_X0) * sizeof(unsigned));
    for (int row = 0; row < 3; ++row) {
        const float x = PW_SETUP_COVER_MARK_X, y = (float)(PW_SETUP_COVER_ROW_Y + row * PW_SETUP_COVER_ROW_STEP);
        if (row < (int)stage) {
            cover_shape(frame, 0, x, y, x, y, 22.0f, 0.0f, green, 1.0f);
            cover_shape(frame, 2, x - 10.0f, y + 1.0f, x - 3.0f, y + 8.0f, 0.0f, 2.6f, white, 1.0f);
            cover_shape(frame, 2, x - 3.0f, y + 8.0f, x + 10.0f, y - 7.0f, 0.0f, 2.6f, white, 1.0f);
        } else if (row == (int)stage) {
            for (unsigned dot = 0; dot < 8; ++dot) {
                const float angle = (float)dot * 0.785398163f - 1.570796327f;
                const unsigned behind = (tick + 8u - dot) % 8u;
                const float dx = x + 15.0f * cosf(angle), dy = y + 15.0f * sinf(angle);
                cover_shape(frame, 0, dx, dy, dx, dy, 3.6f, 0.0f, blue,
                            behind < 6u ? 1.0f - (float)behind * 0.15f : 0.15f);
            }
        } else {
            cover_shape(frame, 1, x, y, x, y, 20.0f, 1.5f, grey, 1.0f);
        }
    }
}
