// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/fsprobe.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include "core/log.h"

namespace fsprobe {

std::vector<Result> run(const std::vector<std::string> &paths) {
    std::vector<Result> results;
    for (const std::string &path : paths) {
        Result r;
        r.path = path;
        struct stat st {};
        if (stat(path.c_str(), &st) != 0) {
            r.error = errno;
        } else {
            r.exists = S_ISDIR(st.st_mode);
            if (DIR *dir = opendir(path.c_str())) {
                r.listable = true;
                r.entries = 0;
                while (readdir(dir) != nullptr && r.entries < 10000) ++r.entries;
                closedir(dir);
            } else {
                r.error = errno;
            }
            // Test d'écriture : crée puis supprime un petit fichier.
            const std::string probe = path + "/.rhythm-ps5-probe";
            int fd = open(probe.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                r.writable = write(fd, "ok", 2) == 2;
                close(fd);
                unlink(probe.c_str());
            } else if (r.error == 0) {
                r.error = errno;
            }
        }
        LOG("fsprobe: %-22s existe=%d liste=%d (%d) ecriture=%d errno=%d", path.c_str(), r.exists, r.listable,
            r.entries, r.writable, r.error);
        results.push_back(r);
    }
    return results;
}

}  // namespace fsprobe
