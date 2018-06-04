/*
 * Copyright 2002-2009, 2014 Beman Dawes
 * Copyright 2001 Dietmar Kuehl
 *
 * Distributed under the Boost Software License, Version 1.0.
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Most of this code was borrowed from Boost in
 * libs/filesystem/src/operations.cpp and
 * libs/filesystem/include/boost/filesystem/operations.hpp.
 */

#ifndef _WIN32
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <windows.h>
#include <winioctl.h>
#include <winnt.h>
#endif

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#include "ignition/msgs/Filesystem.hh"

namespace ignition
{
  namespace msgs
  {
    /// \internal
    /// \brief Private data for the DirIter class.
    class DirIterPrivate
    {
      /// \def current
      /// \brief The current directory item.
      public: std::string current;

      /// \def dirname
      /// \brief The original path to the directory.
      public: std::string dirname;

      /// \def handle
      /// \brief Opaque handle for holding the directory iterator.
      public: void *handle;

      /// \def end
      /// \brief Private variable to indicate whether the iterator has reached
      ///        the end.
      public: bool end;
    };

#ifndef _WIN32

    static const char preferred_separator = '/';

    //////////////////////////////////////////////////
    DirIter::DirIter(const std::string &_in) : dataPtr(new DirIterPrivate)
    {
      this->dataPtr->dirname = _in;

      this->dataPtr->current = "";

      this->dataPtr->handle = opendir(_in.c_str());

      this->dataPtr->end = false;

      if (this->dataPtr->handle == nullptr)
      {
        this->dataPtr->end = true;
      }
      else
      {
        Next();
      }
    }

    //////////////////////////////////////////////////
    void DirIter::Next()
    {
      while (true)
      {
        struct dirent *entry =
          readdir(reinterpret_cast<DIR*>(this->dataPtr->handle)); // NOLINT
        if (!entry)
        {
          this->dataPtr->end = true;
          this->dataPtr->current = "";
          break;
        }

        if ((strcmp(entry->d_name, ".") != 0)
            && (strcmp(entry->d_name, "..") != 0))
        {
          this->dataPtr->current = std::string(entry->d_name);
          break;
        }
      }
    }

    //////////////////////////////////////////////////
    void DirIter::CloseHandle()
    {
      closedir(reinterpret_cast<DIR*>(this->dataPtr->handle));
    }

#else  // Windows

    static const char preferred_separator = '\\';

    //////////////////////////////////////////////////
    static bool not_found_error(int _errval)
    {
      return _errval == ERROR_FILE_NOT_FOUND
        || _errval == ERROR_PATH_NOT_FOUND
        || _errval == ERROR_INVALID_NAME  // "tools/src/:sys:stat.h", "//foo"
        || _errval == ERROR_INVALID_DRIVE  // USB card reader with no card
        || _errval == ERROR_NOT_READY  // CD/DVD drive with no disc inserted
        || _errval == ERROR_INVALID_PARAMETER  // ":sys:stat.h"
        || _errval == ERROR_BAD_PATHNAME  // "//nosuch" on Win64
        || _errval == ERROR_BAD_NETPATH;  // "//nosuch" on Win32
    }

#ifndef MAXIMUM_REPARSE_DATA_BUFFER_SIZE
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE  (16 * 1024)
#endif

    //////////////////////////////////////////////////
    DirIter::DirIter(const std::string &_in) : dataPtr(new DirIterPrivate)
    {
      // use a form of search Sebastian Martel reports will work with Win98
      this->dataPtr->dirname = _in;

      this->dataPtr->current = "";

      this->dataPtr->end = false;

      if (_in.empty())
      {
        // To be compatible with Unix, if given an empty string, assume this
        // is the end.
        this->dataPtr->end = true;
        return;
      }

      std::string dirpath(_in);
      dirpath += (dirpath.empty()
                  || (dirpath[dirpath.size()-1] != '\\'
                      && dirpath[dirpath.size()-1] != '/'
                      && dirpath[dirpath.size()-1] != ':'))? "\\*" : "*";

      WIN32_FIND_DATAA data;
      if ((this->dataPtr->handle = ::FindFirstFileA(dirpath.c_str(), &data))
          == INVALID_HANDLE_VALUE)
      {
        this->dataPtr->handle = nullptr;  // signal eof
        this->dataPtr->end = true;
      }
      else
      {
        this->dataPtr->current = std::string(data.cFileName);
      }
    }

    //////////////////////////////////////////////////
    void DirIter::Next()
    {
      WIN32_FIND_DATAA data;
      if (::FindNextFileA(this->dataPtr->handle, &data) == 0)  // fails
      {
        this->dataPtr->end = true;
        this->dataPtr->current = "";
      }
      else
      {
        this->dataPtr->current = std::string(data.cFileName);
      }
    }

    //////////////////////////////////////////////////
    void DirIter::CloseHandle()
    {
      ::FindClose(this->dataPtr->handle);
    }

#endif  // _WIN32

    //////////////////////////////////////////////////
    const std::string separator(const std::string &_p)
    {
      return _p + preferred_separator;
    }

    //////////////////////////////////////////////////
    DirIter::DirIter() : dataPtr(new DirIterPrivate)
    {
      this->dataPtr->current = "";

      this->dataPtr->dirname = "";

      this->dataPtr->handle = nullptr;

      this->dataPtr->end = true;
    }

    //////////////////////////////////////////////////
    std::string DirIter::operator*() const
    {
      return this->dataPtr->dirname + preferred_separator +
        this->dataPtr->current;
    }

    //////////////////////////////////////////////////
    // prefix operator; note that we don't support the postfix operator
    // because it is complicated to do so
    const DirIter& DirIter::operator++()
    {
      Next();
      return *this;
    }

    //////////////////////////////////////////////////
    bool DirIter::operator!=(const DirIter &_other) const
    {
      return this->dataPtr->end != _other.dataPtr->end;
    }

    //////////////////////////////////////////////////
    DirIter::~DirIter()
    {
      if (this->dataPtr->handle != nullptr)
      {
        CloseHandle();
        this->dataPtr->handle = nullptr;
      }
    }
  }
}