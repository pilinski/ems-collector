/*
 * Buderus EMS data collector
 *
 * Copyright (C) 2011 Danny Baumann <dannybaumann@web.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __SERIALHANDLER_H__
#define __SERIALHANDLER_H__

#include <boost/asio/serial_port.hpp>
#include "IoHandler.h"

class SerialHandler : public IoHandler
{
    public:
	SerialHandler(const std::string& device, Database& db, ValueCache& cache);
	~SerialHandler();

    protected:
	virtual void readStart() {
	    /* Start an asynchronous read and call read_complete when it completes or fails */
	    m_serialPort.async_read_some(boost::asio::buffer(m_recvBuffer, maxReadLength),
					 boost::bind(&SerialHandler::readComplete, this,
						     boost::asio::placeholders::error,
						     boost::asio::placeholders::bytes_transferred));
	}

	virtual void doCloseImpl();

    private:
	boost::asio::serial_port m_serialPort;
};

#endif /* __SERIALHANDLER_H__ */
