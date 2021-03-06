#include "stdafx.h"
#include "DFU.h"
#include "crc32.h"


//generate the suffix and append it to the buffer. 
//Assumes you have 
int appendSuffix(uint8_t* data, int length) 
{
	//generate the suffix
	file_suffix suffix;
	suffix.idVendor = 0xFFFF;
	suffix.idProduct = 0xFFFF;
	suffix.bcdDevice = 0xFFFF;
	//calculate the CRC of the file
	
	uint8_t* suf_buf = (uint8_t*)&suffix;
	memcpy(&data[length], suf_buf, 16);
	uint32_t crc = calc_CRC(data, length+12);
	memcpy(&data[length+12], &crc, 4);
	return 0;
}




DFU::DFU() 
{
	
}

DFU::DFU(uint16_t _vid, uint16_t _pid) 
{
	vid = _vid;
	pid = _pid;

}

DFU::DFU(uint16_t _vid, uint16_t _pid, libusb_device_handle* _dev_handle)
{
	vid = _vid;
	pid = _pid;
	dev_handle = _dev_handle;
}


DFU::~DFU() 
{
	libusb_release_interface(dev_handle, 0);
	libusb_close(dev_handle);
	libusb_exit(ctx);
}

dfu_error DFU::init() 
{
	int error = libusb_init(&ctx); //initialize a library session
	if (error < 0) {

		return (dfu_error)(-99);
	}
	libusb_set_debug(ctx, 3);
	dev_handle = libusb_open_device_with_vid_pid(ctx, vid, pid);
	if (dev_handle == NULL)
	{
		return dfu_error::USB_NO_DEVICE;
	}
	if (libusb_kernel_driver_active(dev_handle, 0))
	{
		libusb_detach_kernel_driver(dev_handle, 0);
	}
	error = libusb_claim_interface(dev_handle, 0);
	if (error < 0)
	{
		return dfu_error::USB_ACCESS;
	}

	//attempt to get dfu functional descriptor, does not work on atmel devices.
	/*
	unsigned char data[9];
	error = libusb_get_descriptor(dev_handle, 0x21, 0, data, 7);
	*/

	return dfu_error::USB_OK;
	

}

dfu_error DFU::detach(int timeout) 
{
	int error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_DEVICE, dfu_request_type::detach, timeout, 4, NULL, 0, 0);
	return dfu_error(error);
}

dfu_error DFU::download(uint8_t* data, int length, int block) 
{

	int error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_DEVICE, dfu_request_type::dnload, block, 4, data, length, 0);
	return (dfu_error)error;
}

//dfu_error DFU::upload(uint8_t* data, int length, int block) {}

dfu_error DFU::eraseChip() 
{
	unsigned char write_command[6] = { 0x04, 0x00, 0xFF, 0,0,0};
	int error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_DEVICE, dfu_request_type::dnload, 0, 4, write_command, 6, 0);
	return dfu_error(error);
}

dfu_error DFU::getStatus(status_response &status)
{

	unsigned char status_buf[6];
	int error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_HOST, dfu_request_type::get_status, 0, 4, status_buf, 6, 0);

	//fill the struct manually because too lazy to test out struct packing, especially with a 3 byte unsigned integer
	status.status = (dfu_status)status_buf[0];
	status.state = (dfu_state)status_buf[4];
	status.poll_timeout = (uint32_t)(status_buf[1] | status_buf[2] << 8 | status_buf[3] << 12);
	status.string_index = (int)status_buf[5];

	return (dfu_error)error;
}

dfu_error DFU::clearStatus() 
{
	int error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_DEVICE, dfu_request_type::clr_status, 0, 4, NULL, 0, 0);
	return (dfu_error)error;
}

dfu_error DFU::getState(dfu_state &state)
{
	unsigned char state_buf;
	int error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_HOST, dfu_request_type::get_state, 0, 4, &state_buf, 1, 0);
	state = (dfu_state)state_buf;
	return (dfu_error)error;
}

dfu_error DFU::abortDFU() 
{
	int error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_DEVICE, dfu_request_type::abort_dfu, 0, 4, NULL, 0, 0);
	return (dfu_error)error;
}


dfu_error DFU::blankCheck(status_response &status, uint16_t start, uint16_t end) 
{
	unsigned char read_cmd[6] = { 0x03, 0x01, unsigned char(start >> 8), unsigned char(start & 0xFF), unsigned char(end >> 8), unsigned char(end & 0xFF)};
	int error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_DEVICE, dfu_request_type::dnload, 0, 4, read_cmd, 6, 0);
	error = getStatus(status);
	return (dfu_error)error;
}

//if reset is true address input will be ignored
dfu_error DFU::startApplication(bool reset, uint16_t address) 
{

	uint8_t reset_byte = ((reset) ? 0 : 1);
	if (reset) {address = 0;}

	unsigned char read_cmd[6] = { 0x04, 0x03, reset_byte, uint8_t(address & 0xFF), uint8_t(address>>8)};

	int error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_DEVICE, dfu_request_type::dnload, 0, 4, read_cmd, 6, 0);
	error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_DEVICE, dfu_request_type::dnload, 0, 4, NULL, 0, 0);

	return (dfu_error)error;
}

dfu_error DFU::readConfig(uint16_t command, uint8_t* data) 
{

	unsigned char read_cmd[6] = { 0x05, uint8_t(command & 0xFF), uint8_t(command >> 8)};
	int error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_DEVICE, dfu_request_type::dnload, 0, 4, read_cmd, 6, 0);
	error = libusb_control_transfer(dev_handle, REQUEST_TYPE_TO_HOST, dfu_request_type::upload, 0, 4, data, 1, 0);
	return dfu_error(error);

}


int generateBlock(uint8_t* buffer, uint8_t* data, uint16_t start_addr, uint16_t data_remaining, uint16_t &data_size)
{
	//figure out length of data
	int max_data_size = 1024 - 32 - sizeof(file_suffix);
	int alignment = start_addr % 32;
	data_size = max_data_size - alignment;
	if (data_remaining < data_size) 
	{
		data_size = data_remaining;
	}
	uint16_t end_addr = start_addr + data_size;
	uint8_t prog[32] = { 0x01, 0x00, uint8_t(start_addr >> 8), uint8_t(start_addr & 0xFF), uint8_t(end_addr >> 8), uint8_t(end_addr & 0xFF) };
	//copy command 32 bytes
	memcpy(buffer, prog, 32);

	//pad data for alignment
	memset(&buffer[32], 0, alignment);

	//copy data
	memcpy(&buffer[32 + alignment], data, data_size);
	//copy suffix

	//return length of packet
	return data_size+ alignment;

}

dfu_error DFU::programData(uint8_t* data, uint16_t start, uint16_t length, uint8_t memory) 
{
	unsigned char* fw_ptr;
	//generate the suffix
			
	int num_packets = ((length - sizeof(file_suffix)) / 1024) + 1;
	int error = 0;
	int block;

	status_response status;
	uint16_t end_addr = start + 1024 - 32 - 16;
	unsigned char prg_start[32] = {0x01, 0x00, 0x00, 0x00, uint8_t(end_addr>>8), uint8_t(end_addr & 0xFF) };

	fw_ptr = &data[0];

	for (block = 0; block < num_packets; block++)
	{
		uint8_t block_buf[1024];
		uint16_t size_sent;
		int packet_length = generateBlock(block_buf, fw_ptr, start, length - (fw_ptr - data), size_sent);
		fw_ptr += packet_length - size_sent;

		//send packet
		error = download(block_buf, packet_length + 32 + 16, block);
		//get status
		if (error < 0)
		{
			return dfu_error(error);
		}
		error = getStatus(status);
		if (error < 0) 
		{
			return dfu_error(error);
		}
		start += size_sent;
	}

	//zlp
	error = download(NULL, 0, 0);

	return (dfu_error)error;
}