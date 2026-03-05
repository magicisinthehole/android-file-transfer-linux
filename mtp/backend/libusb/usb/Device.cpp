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
#include <usb/call.h>
#include <mtp/ByteArray.h>
#include <mtp/log.h>

namespace mtp { namespace usb
{

	Device::Device(ContextPtr context, libusb_device_handle * handle): _context(context), _handle(handle)
	{
	}

	void Device::Reset()
	{ }

	Device::~Device()
	{
		libusb_close(_handle);
	}

	int Device::GetConfiguration() const
	{
		int config;
		USB_CALL(libusb_get_configuration(_handle, &config));
		return config;
	}

	void Device::SetConfiguration(int idx)
	{
		USB_CALL(libusb_set_configuration(_handle, idx));
	}

	InterfaceToken::InterfaceToken(libusb_device_handle *handle, int index): _handle(handle), _index(index)
	{
		USB_CALL(libusb_claim_interface(handle, index));
		USB_CALL(libusb_set_interface_alt_setting(handle, index, 0));
	}

	InterfaceToken::~InterfaceToken()
	{ libusb_release_interface(_handle, _index); }

	InterfaceTokenPtr Device::ClaimInterface(const InterfacePtr & interface)
	{ return std::make_shared<InterfaceToken>(_handle, interface->GetIndex()); }

	void Device::WriteBulk(const EndpointPtr & ep, const IObjectInputStreamPtr &inputStream, int)
	{
		size_t transferSize = ep->GetMaxPacketSize();
		ByteArray buffer(transferSize);
		size_t bytesRead;
		do
		{
			bytesRead = inputStream->Read(buffer.data(), buffer.size());
			int tr = 0;
			USB_CALL(libusb_bulk_transfer(_handle, ep->GetAddress(),
				const_cast<u8*>(buffer.data()), bytesRead, &tr, 0));
		}
		while(bytesRead == transferSize);
	}

	void Device::ReadBulk(const EndpointPtr & ep, const IObjectOutputStreamPtr &outputStream, int)
	{
		size_t transferSize = ep->GetMaxPacketSize();
		ByteArray buffer(transferSize);
		int tr;
		do
		{
			tr = 0;
			USB_CALL(libusb_bulk_transfer(_handle, ep->GetAddress(), buffer.data(), buffer.size(), &tr, 0));
			outputStream->Write(buffer.data(), tr);
		}
		while(tr == (int)transferSize);
	}

	void Device::ReadControl(u8 type, u8 req, u16 value, u16 index, ByteArray &data, int timeout)
	{
		int r = libusb_control_transfer(_handle, type, req, value, index, data.data(), data.size(), timeout);
		if (r == LIBUSB_ERROR_NO_DEVICE) throw DeviceNotFoundException();
		if (r < 0) throw Exception("libusb_control_transfer", r);
	}

	void Device::WriteControl(u8 type, u8 req, u16 value, u16 index, const ByteArray &data, int timeout)
	{
		int r = libusb_control_transfer(_handle, type, req, value, index, const_cast<u8 *>(data.data()), data.size(), timeout);
		if (r == LIBUSB_ERROR_NO_DEVICE) throw DeviceNotFoundException();
		if (r < 0) throw Exception("libusb_control_transfer", r);
	}

	void Device::ClearHalt(const EndpointPtr & ep)
	{
		USB_CALL(libusb_clear_halt(_handle, ep->GetAddress()));
	}

	std::string Device::GetString(int idx) const
	{
		unsigned char buffer[4096];
		int r = libusb_get_string_descriptor_ascii(_handle, idx, buffer, sizeof(buffer));
		if (r < 0)
				throw Exception("libusb_get_string_descriptor_ascii", r);
		return std::string(buffer, buffer + r);
	}

}}
