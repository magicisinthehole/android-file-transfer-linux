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

#include <usb/Device.h>
#include <usb/Context.h>
#include <usb/Interface.h>
#include <mtp/ByteArray.h>
#include <mtp/log.h>
#include <mtp/usb/TimeoutException.h>
#include <stdexcept>

namespace mtp { namespace usb
{

	Device::Device(ContextPtr context, HANDLE deviceHandle, WINUSB_INTERFACE_HANDLE winusbHandle, const std::string& devicePath)
		: _context(context), _deviceHandle(deviceHandle), _winusbHandle(winusbHandle), _devicePath(devicePath)
	{
		debug("Windows USB Device: Created for ", devicePath);
	}

	Device::~Device()
	{
		if (_winusbHandle != INVALID_HANDLE_VALUE && _winusbHandle != NULL)
		{
			WinUsb_Free(_winusbHandle);
			_winusbHandle = NULL;
		}

		if (_deviceHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(_deviceHandle);
			_deviceHandle = INVALID_HANDLE_VALUE;
		}

		debug("Windows USB Device: Destroyed");
	}

	void Device::Reset()
	{
		if (_winusbHandle == NULL || _winusbHandle == INVALID_HANDLE_VALUE)
		{
			throw std::runtime_error("WinUsb_ResetPipe failed: Invalid handle");
		}

		debug("Windows USB Device: Reset requested (resetting pipes)");
	}

	int Device::GetConfiguration() const
	{
		return 1;
	}

	void Device::SetConfiguration(int idx)
	{
		debug("Windows USB Device: SetConfiguration(", idx, ") - not supported by WinUSB");
	}

	InterfaceToken::InterfaceToken(DevicePtr device, int index)
		: _device(device), _index(index)
	{
		// Interface is already claimed when WinUSB handle is created
		debug("Windows USB Interface: Claimed interface ", index);
	}

	InterfaceToken::~InterfaceToken()
	{
		// Interface is released when WinUSB handle is freed
		debug("Windows USB Interface: Released interface ", _index);
	}

	InterfaceTokenPtr Device::ClaimInterface(const InterfacePtr & iface)
	{
		return std::make_shared<InterfaceToken>(shared_from_this(), iface->GetIndex());
	}

	void Device::WriteBulk(const EndpointPtr & ep, const IObjectInputStreamPtr &inputStream, int timeout)
	{
		if (_winusbHandle == NULL || _winusbHandle == INVALID_HANDLE_VALUE)
		{
			throw std::runtime_error("WriteBulk failed: Invalid WinUSB handle");
		}

		// Read data from input stream
		size_t size = inputStream->GetSize();
		ByteArray data(size);
		inputStream->Read(data.data(), size);

		// Bulk pipes get the WinUSB default (0 = infinite, kernel does not cancel).
		// macOS IOKit and Linux usbdevfs both block until the device responds; on
		// Windows, an app-set PIPE_TRANSFER_TIMEOUT makes the kernel cancel the
		// transfer mid-flight, which loses bytes the device already clocked onto
		// the bus and breaks MTP transport state. Only honor the timeout for
		// interrupt pipes, where ReadInterrupt depends on responsive polling.
		if (timeout > 0 && ep->GetType() == EndpointType::Interrupt)
		{
			ULONG timeoutMs = static_cast<ULONG>(timeout);
			WinUsb_SetPipePolicy(_winusbHandle, ep->GetAddress(), PIPE_TRANSFER_TIMEOUT, sizeof(ULONG), &timeoutMs);
		}
		else if (ep->GetType() == EndpointType::Bulk)
		{
			ULONG zero = 0;
			WinUsb_SetPipePolicy(_winusbHandle, ep->GetAddress(), PIPE_TRANSFER_TIMEOUT, sizeof(ULONG), &zero);
		}

		// Perform bulk write
		ULONG bytesWritten = 0;
		BOOL result = WinUsb_WritePipe(
			_winusbHandle,
			ep->GetAddress(),
			const_cast<u8*>(data.data()),
			static_cast<ULONG>(data.size()),
			&bytesWritten,
			NULL
		);

		if (!result)
		{
			DWORD lastError = GetLastError();
			if (lastError == ERROR_SEM_TIMEOUT)
				throw TimeoutException("WriteBulk timeout");
			throw std::runtime_error("WinUsb_WritePipe failed with error: " + std::to_string(lastError));
		}

		if (bytesWritten != data.size())
		{
			throw std::runtime_error("Short write: expected " + std::to_string(data.size()) +
			                        " bytes, wrote " + std::to_string(bytesWritten));
		}
	}

	void Device::ReadBulk(const EndpointPtr & ep, const IObjectOutputStreamPtr &outputStream, int timeout)
	{
		if (_winusbHandle == NULL || _winusbHandle == INVALID_HANDLE_VALUE)
		{
			throw std::runtime_error("ReadBulk failed: Invalid WinUSB handle");
		}

		// See WriteBulk above — only interrupt pipes get an enforced timeout.
		if (timeout > 0 && ep->GetType() == EndpointType::Interrupt)
		{
			ULONG timeoutMs = static_cast<ULONG>(timeout);
			WinUsb_SetPipePolicy(_winusbHandle, ep->GetAddress(), PIPE_TRANSFER_TIMEOUT, sizeof(ULONG), &timeoutMs);
		}
		else if (ep->GetType() == EndpointType::Bulk)
		{
			ULONG zero = 0;
			WinUsb_SetPipePolicy(_winusbHandle, ep->GetAddress(), PIPE_TRANSFER_TIMEOUT, sizeof(ULONG), &zero);
		}

		// Read in chunks sized to match the Linux usbdevfs backend
		// (max(MaxPacketSize, 4096-rounded-down-to-MaxPacketSize)). A single
		// 512 KB ReadPipe pends the pipe until short-packet-or-timeout, which
		// deadlocks PipePacketer's transaction-matching loop when the device
		// queues unrelated PPP/TCP frames ahead of the matching response.
		// Smaller chunks return promptly per burst so the layer above can
		// iterate through queued data.
		const size_t packetSize = ep->GetMaxPacketSize();
		size_t chunkSize = (size_t)4096 / packetSize * packetSize;
		if (chunkSize < packetSize)
			chunkSize = packetSize;
		ByteArray buffer(chunkSize);
		ULONG bytesRead = 0;

		do
		{
			BOOL result = WinUsb_ReadPipe(
				_winusbHandle,
				ep->GetAddress(),
				buffer.data(),
				static_cast<ULONG>(buffer.size()),
				&bytesRead,
				NULL
			);

			if (!result)
			{
				DWORD lastError = GetLastError();
				if (lastError == ERROR_SEM_TIMEOUT)
				{
					throw TimeoutException("ReadBulk timeout");
				}
				throw std::runtime_error("WinUsb_ReadPipe failed with error: " + std::to_string(lastError));
			}

			if (bytesRead > 0)
			{
				outputStream->Write(buffer.data(), bytesRead);
			}
		}
		while (bytesRead == chunkSize); // Continue while the chunk filled
	}

	void Device::ReadControl(u8 type, u8 req, u16 value, u16 index, ByteArray &data, int timeout)
	{
		if (_winusbHandle == NULL || _winusbHandle == INVALID_HANDLE_VALUE)
		{
			throw std::runtime_error("ReadControl failed: Invalid WinUSB handle");
		}

		WINUSB_SETUP_PACKET setupPacket;
		setupPacket.RequestType = type;
		setupPacket.Request = req;
		setupPacket.Value = value;
		setupPacket.Index = index;
		setupPacket.Length = static_cast<USHORT>(data.size());

		ULONG bytesRead = 0;
		BOOL result = WinUsb_ControlTransfer(
			_winusbHandle,
			setupPacket,
			data.data(),
			static_cast<ULONG>(data.size()),
			&bytesRead,
			NULL
		);

		if (!result)
		{
			DWORD lastError = GetLastError();
			throw std::runtime_error("WinUsb_ControlTransfer (read) failed with error: " + std::to_string(lastError));
		}
	}

	void Device::WriteControl(u8 type, u8 req, u16 value, u16 index, const ByteArray &data, int timeout)
	{
		if (_winusbHandle == NULL || _winusbHandle == INVALID_HANDLE_VALUE)
		{
			throw std::runtime_error("WriteControl failed: Invalid WinUSB handle");
		}

		WINUSB_SETUP_PACKET setupPacket;
		setupPacket.RequestType = type;
		setupPacket.Request = req;
		setupPacket.Value = value;
		setupPacket.Index = index;
		setupPacket.Length = static_cast<USHORT>(data.size());

		ULONG bytesWritten = 0;
		BOOL result = WinUsb_ControlTransfer(
			_winusbHandle,
			setupPacket,
			const_cast<u8*>(data.data()),
			static_cast<ULONG>(data.size()),
			&bytesWritten,
			NULL
		);

		if (!result)
		{
			DWORD lastError = GetLastError();
			throw std::runtime_error("WinUsb_ControlTransfer (write) failed with error: " + std::to_string(lastError));
		}
	}

	void Device::ClearHalt(const EndpointPtr & ep)
	{
		if (_winusbHandle == NULL || _winusbHandle == INVALID_HANDLE_VALUE)
		{
			throw std::runtime_error("ClearHalt failed: Invalid WinUSB handle");
		}

		BOOL result = WinUsb_ResetPipe(_winusbHandle, ep->GetAddress());
		if (!result)
		{
			DWORD lastError = GetLastError();
			mtp::error("WinUsb_ResetPipe failed with error: ", lastError);
		}
	}

	std::string Device::GetString(int idx) const
	{
		if (_winusbHandle == NULL || _winusbHandle == INVALID_HANDLE_VALUE)
		{
			return std::string();
		}

		if (idx == 0)
		{
			return std::string();
		}

		UCHAR buffer[256];
		ULONG bytesRead = 0;

		BOOL result = WinUsb_GetDescriptor(
			_winusbHandle,
			USB_STRING_DESCRIPTOR_TYPE,
			static_cast<UCHAR>(idx),
			0,  // Language ID (0 = default)
			buffer,
			sizeof(buffer),
			&bytesRead
		);

		if (!result || bytesRead < 2)
		{
			return std::string();
		}

		UCHAR length = buffer[0];
		if (length > bytesRead)
			length = static_cast<UCHAR>(bytesRead);

		int wcharCount = (length - 2) / 2;
		if (wcharCount <= 0)
			return std::string();

		const wchar_t* wstr = reinterpret_cast<const wchar_t*>(buffer + 2);
		int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wstr, wcharCount, NULL, 0, NULL, NULL);
		if (utf8Size <= 0)
			return std::string();

		std::string str(utf8Size, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wstr, wcharCount, &str[0], utf8Size, NULL, NULL);
		return str;
	}

}}
