/*
 * MihawkOS - "Install runtime from storage" (docs/SELF_INSTALL.md, phase 5).
 *
 * The title folder pins the runtime's manifest (runtime-pack.txt, tools/pack-runtime.py) and carries its pieces, in
 * runtime-pieces/ (tools/package-install.py puts them there: one folder to copy, the owner, 2026-10-31). Game helpers
 * cannot read the title folder, so src/pw_pack.h installs the pieces into /data/ps5proton on a thread of its own while
 * the launcher draws its progress, checking every piece's and every file's SHA-256 against the manifest. Every failure
 * is a sentence for the screen; the log gets the same line with the code and errno.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PROTON_INSTALL_H
#define PROTON_INSTALL_H

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../../src/pw_pack.h"

struct ProtonInstall {
	enum Phase { unavailable, ready, checking, running, done, failed } phase{ unavailable };
	/* What `message` says, for a screen that words it itself (the first-launch tour, in the user's language). */
	enum Problem {
		none,
		no_manifest,     /* this build pins no runtime manifest, or a damaged one */
		no_pieces,       /* the title folder holds none of the pieces (a development build, or a copy without them) */
		partial,         /* `have` of `total` pieces */
		no_space,        /* never reported by the title: it cannot learn the free space (a full disk is storage_full) */
		read_failed,     /* a piece could not be read */
		damaged,         /* a piece or a file is not the release's */
		storage_full,    /* the console's storage refused for space */
		write_failed,    /* the console's storage refused for another reason, or a link is in the way */
		cancelled,       /* proton_install_cancel */
		network,         /* a download: no name, no connection, or the connection stopped (src/pw_https.h) */
		certificate,     /* a download: the server's certificate is untrusted or for another name */
		clock,           /* a download: the certificate is not valid now, often a console clock that is wrong */
		server,          /* a download: the server refused or answered nonsense */
	} problem{ none };
	std::string source;      /* the folder the pieces come from */
	std::string message;     /* for the screen: what is ready, missing or wrong */
	uint64_t bytes{ 0 };      /* what the install holds once written (the manifest's plain size), known from find on */
	uint64_t needed{ 0 }, available{ 0 };   /* available: 0, not known (a title process has no statfs) */
	size_t have{ 0 }, total{ 0 };
	PwPackProgress progress{};
	double seconds{ 0.0 };   /* since the install started */
};

/* Look again for the pieces (not while an install runs). Needs /data. The title folder's runtime-pieces/, then the
 * release's download when it holds none; `only` "title": that folder alone, never a download (the guided setup). */
ProtonInstall proton_install_find(const char *only = nullptr);
/* Start the install proton_install_find found ready; returns at once. */
void proton_install_start();
/* Stop at the next piece, or within it: what is installed already is kept, the phase ends failed and cancelled. */
void proton_install_cancel();
/* Where the install is now. */
ProtonInstall proton_install_state();

/* One pack's pieces from `source` (a folder, or an http(s) base ending in '/') into `root`: the loop behind
 * proton_install_start, which the updater runs for a title pack (proton_update.h). On the caller's thread. */
struct ProtonPackJob {
	const PwPackManifest *manifest{ nullptr };
	std::string root, source;
	std::vector<std::string> old;            /* what the last install held: deleted where this manifest lacks it */
	const char *record{ nullptr };           /* where the manifest's own copy goes, under root */
	const std::atomic<bool> *cancel{ nullptr };
	std::function<void(PwPack *)> publish;   /* the open pack, then nullptr: another thread reads its progress */
};
/* "" once installed; else the sentence for the screen, with `problem` its kind. `progress` is where it ended. */
std::string proton_pack_install(const ProtonPackJob &job, ProtonInstall::Problem &problem, PwPackProgress &progress);

#endif
