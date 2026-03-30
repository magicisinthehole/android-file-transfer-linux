/*
    This file is part of Android File Transfer For Linux.
    Copyright (C) 2015-2020  Vladimir Menshakov

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License,
    or (at your option) any later version.

    This library is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library; if not, write to the Free Software Foundation,
    Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef AFTL_CLI_POSIXSTREAMS_H
#define AFTL_CLI_POSIXSTREAMS_H

#include <mtp/ptp/IObjectStream.h>

#include <functional>

#ifdef _WIN32
	#include <io.h>
	#include <fcntl.h>
	#include <sys/stat.h>
	#include <sys/utime.h>
	#include <stdio.h>
	#define posix_open(path, flags, ...) _open(path, flags | _O_BINARY, __VA_ARGS__)
	#define posix_read _read
	#define posix_write _write
	#define posix_close _close
	#define posix_stat _stat
	#define posix_stat_t struct _stat
	#define posix_utime _utime
	#define posix_utimbuf _utimbuf
	#ifndef _SSIZE_T_DEFINED
	#define _SSIZE_T_DEFINED
	typedef int ssize_t;
	#endif
#else
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <fcntl.h>
	#include <stdio.h>
	#include <unistd.h>
	#include <utime.h>
	#define posix_open open
	#define posix_read read
	#define posix_write write
	#define posix_close close
	#define posix_stat stat
	#define posix_stat_t struct stat
	#define posix_utime utime
	#define posix_utimbuf utimbuf
#endif

namespace cli
{
	class BaseObjectStream: public mtp::CancellableStream
	{
		std::function<void (mtp::u64, mtp::u64)>	_progressReporter;
		mtp::u64									_current, _total;

	public:
		BaseObjectStream(): _current(0), _total(0) { }
		virtual ~BaseObjectStream() = default;

		void SetProgressReporter(const decltype(_progressReporter) & pr)
		{ _progressReporter = pr; }

		void SetTotal(mtp::u64 total)
		{ _current = 0; _total = total; }

	protected:
		void Report(mtp::u64 delta)
		{
			if (_progressReporter)
			{
				_current += delta;
				_progressReporter(_current, _total);
			}
		}
	};

	class ObjectInputStream final:
		public BaseObjectStream,
		public virtual mtp::IObjectInputStream
	{
		int			_fd;
		mtp::u64	_size;

	public:
		ObjectInputStream(const std::string &fname) : _fd(posix_open(fname.c_str(), O_RDONLY))
		{
			if (_fd < 0)
				throw std::runtime_error("cannot open file: " + fname);

			posix_stat_t st;
			if (posix_stat(fname.c_str(), &st) != 0)
				throw std::runtime_error("stat failed");
			_size = st.st_size;
		}

		~ObjectInputStream()
		{ posix_close(_fd); }

		mtp::u64 GetSize() const
		{ return _size; }

		virtual size_t Read(mtp::u8 *data, size_t size)
		{
			CheckCancelled();
			ssize_t r = posix_read(_fd, data, size);
			if (r < 0)
				throw std::runtime_error("read failed");
			Report(r);
			return r;
		}
	};

	class ObjectOutputStream final:
		public BaseObjectStream,
		public mtp::IObjectOutputStream
	{
		int		_fd;

	public:
		ObjectOutputStream(const std::string &fname) :
			_fd(posix_open(fname.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644))
		{
			if (_fd < 0)
				throw std::runtime_error("cannot open file: " + fname);
		}

		~ObjectOutputStream()
		{ posix_close(_fd); }

		virtual size_t Write(const mtp::u8 *data, size_t size)
		{
			CheckCancelled();
			ssize_t r = posix_write(_fd, data, size);
			if (r < 0)
				throw std::runtime_error("write failed");
			Report(r);
			return r;
		}

		static void SetModificationTime(const std::string &fname, time_t mtime)
		{
			posix_utimbuf buf = {};
			buf.actime = time(nullptr);
			buf.modtime = mtime;
			if (posix_utime(fname.c_str(), &buf) != 0)
				throw mtp::system_error("utime");
		}
	};

}

#endif
