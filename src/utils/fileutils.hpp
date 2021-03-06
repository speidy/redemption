/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   Product name: redemption, a FLOSS RDP proxy
   Copyright (C) Wallix 2013
   Author(s): Christophe Grosjean, Raphael Zhou

   File related utility functions
*/

#ifndef _REDEMPTION_UTILS_FILEUTILS_HPP_
#define _REDEMPTION_UTILS_FILEUTILS_HPP_

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <cstdio>
#include <cstddef>
#include <cerrno>

#include "log.hpp"
#include "error.hpp"

static inline int filesize(const char * path)
{
    struct stat sb;
    int status = stat(path, &sb);
    if (status >= 0){
        return sb.st_size;
    }
    return -1;
}

static inline bool file_exist(const char * filename) {
    struct stat buffer;
    return (stat (filename, &buffer) == 0);
}

static inline
void ParsePath(const char * fullpath, std::string & directory,
              std::string & filename, std::string & extension) {
    const char * end_of_directory = strrchr(fullpath, '/');
    if (end_of_directory > fullpath) {
        directory.assign(fullpath, end_of_directory - fullpath + 1);
    }

    const char * begin_of_filename =
        (end_of_directory ? end_of_directory + 1 : fullpath);
    const char * end_of_filename   =
        [begin_of_filename] () {
            const char * dot = strrchr(begin_of_filename, '.');
            if (!dot || (dot == begin_of_filename)) {
                return begin_of_filename + strlen(begin_of_filename) - 1;
            }
            return dot - 1;
        } ();

    if (end_of_filename > begin_of_filename) {
        filename.assign(begin_of_filename,
            end_of_filename - begin_of_filename + 1);
    }

    if (*(end_of_filename + 1)) {
        extension = end_of_filename + 1;
    }
}

static inline
void MakePath(std::string & fullpath, const char * directory,
              const char * filename, const char * extension) {
    fullpath = (directory ? directory : "");
    if (fullpath.size() && (fullpath.back() != '/')) { fullpath += '/'; }
    if (filename) { fullpath += filename; }
    if (extension && *extension) {
        if (*extension != '.') { fullpath += '.'; }
        fullpath += extension;
    }
}

static inline bool canonical_path( const char * fullpath, char * path, size_t path_len
                                 , char * basename, size_t basename_len, char * extension
                                 , size_t extension_len, uint32_t verbose = 255)
{
    const char * end_of_path = strrchr(fullpath, '/');
    if (end_of_path){
        if (static_cast<size_t>(end_of_path + 1 - fullpath) <= path_len) {
            memcpy(path, fullpath, end_of_path + 1 - fullpath);
            path[end_of_path + 1 - fullpath] = 0;
        }
        else {
            if (verbose >= 255) {
                LOG(LOG_ERR, "canonical_path : Path too long for the buffer\n");
            }
            return false;
        }
        const char * start_of_extension = strrchr(end_of_path + 1, '.');
        if (start_of_extension){
            snprintf(extension, extension_len, "%s", start_of_extension);
            //strcpy(extension, start_of_extension);
            if (start_of_extension > end_of_path + 1){
                if (static_cast<size_t>(start_of_extension - end_of_path - 1) <= basename_len) {
                    memcpy(basename, end_of_path + 1, start_of_extension - end_of_path - 1);
                    basename[start_of_extension - end_of_path - 1] = 0;
                }
                else {
                    if (verbose >= 255) {
                        LOG(LOG_ERR, "canonical_path : basename too long for the buffer\n");
                    }
                    return false;
                }
            }
            // else no basename : leave output buffer for name untouched
        }
        else {
            if (end_of_path[1]){
                snprintf(basename, basename_len, "%s", end_of_path + 1);
                //strcpy(basename, end_of_path + 1);
                // default extension : leave whatever is in extension output buffer
            }
            else {
                // default name : leave whatever is in name output buffer
                // default extension : leave whatever is in extension output buffer
            }
        }
    }
    else {
        // default path : leave whatever is in path output buffer
        const char * start_of_extension = strrchr(fullpath, '.');
        if (start_of_extension){
            snprintf(extension, extension_len, "%s", start_of_extension);
            // strcpy(extension, start_of_extension);
            if (start_of_extension > fullpath){
                if (static_cast<size_t>(start_of_extension - fullpath) <= basename_len) {
                    memcpy(basename, fullpath, start_of_extension - fullpath);
                    basename[start_of_extension - fullpath] = 0;
                }
                else {
                    if (verbose >= 255) {
                        LOG(LOG_ERR, "canonical_path : basename too long for the buffer\n");
                    }
                    return false;
                }
            }
            // else no basename : leave output buffer for name untouched
        }
        else {
            if (fullpath[0]){
                snprintf(basename, basename_len, "%s", fullpath);
                // strcpy(basename, fullpath);
                // default extension : leave whatever is in extension output buffer
            }
            else {
                // default name : leave whatever is in name output buffer
                // default extension : leave whatever is in extension output buffer
            }
        }
    }
    if (verbose >= 255) {
        LOG(LOG_INFO, "canonical_path : %s%s%s\n", path, basename, extension);
    }
    return true;
}

static inline char * pathncpy(char * dest, const char * src, const size_t n) {
    TODO("use error return value instead of raisong an exception. The returned pointer is only used in tests, not code anyway");
    size_t src_len = strnlen(src, n);
    if (src_len >= n) {
        LOG(LOG_INFO, "can't copy path, no room in dest path (available %d): %s\n", static_cast<int>(n), src);
        throw Error(ERR_PATH_TOO_LONG);
    }
    if ((src_len == 0) && (n >= 3)){
        memcpy(dest, "./", 3);
    }
    else {
        memcpy(dest, src, src_len + 1);
        if (src[src_len - 1] != '/') {
            if (src_len + 1 >= n) {
                LOG(LOG_INFO, "can't copy path, no room in dest path to add trailing slash: %s\n", src);
                throw Error(ERR_PATH_TOO_LONG);
            }
            dest[src_len] = '/';
            dest[src_len+1] = 0;
        }
    }
    return dest;
}

static inline void clear_files_flv_meta_png(const char * path, const char * prefix, uint32_t verbose = 255)
{
    struct D {
        DIR * d;

        ~D() { closedir(d); }
        operator DIR * () const { return d; }
    } d{opendir(path)};

    if (d){
//        char static_buffer[8192];
        char buffer[8192];
        size_t path_len = strlen(path);
        size_t prefix_len = strlen(prefix);
        size_t file_len = 1024;
//        size_t file_len = pathconf(path, _PC_NAME_MAX) + 1;
//        char * buffer = static_buffer;

/*
        if (file_len < 4000){
            if (verbose >= 255) {
                LOG(LOG_WARNING, "File name length is in normal range (%u), using static buffer", static_cast<unsigned>(file_len));
            }
            file_len = 4000;
        }
        else {
            if (verbose >= 255) {
                LOG(LOG_WARNING, "Max file name too large (%u), using dynamic buffer", static_cast<unsigned>(file_len));
            }

            char * buffer = (char*)malloc(file_len + path_len + 1);
            if (!buffer){
                if (verbose >= 255) {
                    LOG(LOG_WARNING, "Memory allocation failed for file name buffer, using static buffer");
                }
                buffer = static_buffer;
                file_len = 4000;
            }
        }
*/
        if (file_len + path_len + 1 > sizeof(buffer)) {
            LOG(LOG_WARNING, "Path len %u > %u", file_len + path_len + 1, sizeof(buffer));
            return;
        }
        strncpy(buffer, path, file_len + path_len + 1);
        if (buffer[path_len] != '/'){
            buffer[path_len] = '/'; path_len++; buffer[path_len] = 0;
        }

        TODO("size_t len = offsetof(struct dirent, d_name) + NAME_MAX + 1 ?")
        const size_t len = offsetof(struct dirent, d_name) + file_len;
        struct E {
            dirent * entryp;

            ~E() { free(entryp); }
            operator dirent * () const { return entryp; }
            dirent * operator -> () const { return entryp; }
        } entryp{static_cast<struct dirent *>(malloc(len))};
        if (!entryp){
            LOG(LOG_WARNING, "Memory allocation failed for entryp, exiting file cleanup code");
            return;
        }
        struct dirent * result;
        for (readdir_r(d, entryp, &result) ; result ; readdir_r(d, entryp, &result)) {
            if ((0 == strcmp(entryp->d_name, ".")) || (0 == strcmp(entryp->d_name, ".."))){
                continue;
            }

            if (strncmp(entryp->d_name, prefix, prefix_len)){
                continue;
            }

            strncpy(buffer + path_len, entryp->d_name, file_len);
            const char * eob = buffer + path_len + strlen(entryp->d_name);
            const bool extension = ((eob[-4] == '.') && (eob[-3] == 'f') && (eob[-2] == 'l') && (eob[-1] == 'v'))
                          || ((eob[-4] == '.') && (eob[-3] == 'p') && (eob[-2] == 'n') && (eob[-1] == 'g'))
                          || ((eob[-5] == '.') && (eob[-4] == 'm') && (eob[-3] == 'e') && (eob[-2] == 't') && (eob[-1] == 'a'))
                          || ((eob[-4] == '.') && (eob[-3] == 'p') && (eob[-2] == 'g') && (eob[-1] == 's'))
                          ;

            if (!extension){
                continue;
            }

            struct stat st;
            if (stat(buffer, &st) < 0){
                if (verbose >= 255) {
                    LOG(LOG_WARNING, "Failed to read file %s [%u: %s]\n", buffer, errno, strerror(errno));
                }
                continue;
            }
            if (unlink(buffer) < 0){
                LOG(LOG_WARNING, "Failed to remove file %s", buffer, errno, strerror(errno));
            }
        }
    }
    else {
        LOG(LOG_WARNING, "Failed to open directory %s [%u: %s]", path, errno, strerror(errno));
    }
}

static inline int _internal_make_directory(const char *directory, mode_t mode, const int groupid, uint32_t verbose) {
    struct stat st;
    int status = 0;

    if ((directory[0] != 0) && strcmp(directory, ".") && strcmp(directory, "..")) {
        if (stat(directory, &st) != 0) {
            /* Directory does not exist. */
            if ((mkdir(directory, mode) != 0) && (errno != EEXIST)) {
                status = -1;
                if (verbose >= 255) {
                    LOG(LOG_ERR, "failed to create directory %s : %s [%u]", directory, strerror(errno), errno);
                }
            }
            if (groupid){
                if (chown(directory, static_cast<uid_t>(-1), groupid) < 0){
                    if (verbose >= 255) {
                        LOG(LOG_ERR, "can't set directory %s group to %u : %s [%u]", directory, groupid, strerror(errno), errno);
                    }
                }
            }

        }
        else if (!S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            if (verbose >= 255) {
                LOG(LOG_ERR, "expecting directory name, got filename, for %s");
            }
            status = -1;
        }
    }
    return status;
}



TODO("Add unit tests for recursive_create_directory")
static inline int recursive_create_directory(const char *directory, mode_t mode, const int groupid, uint32_t verbose = 255) {
    int    status;
    char * copy_directory;
    char * pTemp;
    char * pSearch;

    status         = 0;
    copy_directory = strdup(directory);

    if (copy_directory == nullptr) {
        return -1;
    }

    for ( pTemp = copy_directory
        ; (status == 0) && ((pSearch = strchr(pTemp, '/')) != nullptr)
        ; pTemp = pSearch + 1) {
        if (pSearch == pTemp) {
            continue;
        }

        pSearch[0] = 0;
        status = _internal_make_directory(copy_directory, mode, groupid, verbose);
        *pSearch = '/';
    }

    if (status == 0) {
        status = _internal_make_directory(directory, mode, groupid, verbose);
    }

    free(copy_directory);

    return status;
}

static inline int recursive_delete_directory(const char * directory_path) {
    DIR * dir = opendir(directory_path);

    int return_value = 0;

    if (dir) {
        struct dirent * ent;

        while (!return_value && (ent = readdir(dir)))
        {
            /* Skip the names "." and ".." as we don't want to recurse on them. */
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
                continue;
            }

            char * entry_path;
            size_t entry_path_length;

            entry_path_length = strlen(directory_path) + strlen(ent->d_name) + 2;
            entry_path = reinterpret_cast<char *>(alloca(entry_path_length));

            if (entry_path) {
                struct stat statbuf;

                snprintf(entry_path, entry_path_length, "%s/%s", directory_path, ent->d_name);

                if (!stat(entry_path, &statbuf)) {
                    if (S_ISDIR(statbuf.st_mode)) {
                        return_value = recursive_delete_directory(entry_path);
                    }
                    else {
                        return_value = unlink(entry_path);
                    }
                }
            }
        }

        closedir(dir);
    }

    if (!return_value) {
        return_value = rmdir(directory_path);
    }

    return return_value;
}

#endif
