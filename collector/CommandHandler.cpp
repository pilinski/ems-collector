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

#include <asm/byteorder.h>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include "CommandHandler.h"

CommandHandler::CommandHandler(TcpHandler& handler,
			       boost::asio::ip::tcp::endpoint& endpoint) :
    m_handler(handler),
    m_acceptor(handler, endpoint),
    m_sendTimer(handler)
{
    startAccepting();
}

CommandHandler::~CommandHandler()
{
    m_acceptor.close();
    std::for_each(m_connections.begin(), m_connections.end(),
		  boost::bind(&CommandConnection::close, _1));
    m_connections.clear();
    m_sendTimer.cancel();
}

void
CommandHandler::handleAccept(CommandConnection::Ptr connection,
			     const boost::system::error_code& error)
{
    if (error) {
	if (error != boost::asio::error::operation_aborted) {
	    std::cerr << "Accept error: " << error.message() << std::endl;
	}
	return;
    }

    startConnection(connection);
    startAccepting();
}

void
CommandHandler::startConnection(CommandConnection::Ptr connection)
{
    m_connections.insert(connection);
    connection->startRead();
}

void
CommandHandler::stopConnection(CommandConnection::Ptr connection)
{
    m_connections.erase(connection);
    connection->close();
}

void
CommandHandler::handlePcMessage(const EmsMessage& message)
{
    m_lastCommTimes[message.getSource()] = boost::posix_time::microsec_clock::universal_time();

    std::for_each(m_connections.begin(), m_connections.end(),
		  boost::bind(&CommandConnection::handlePcMessage,
			      _1, message));
}

void
CommandHandler::startAccepting()
{
    CommandConnection::Ptr connection(new CommandConnection(*this));
    m_acceptor.async_accept(connection->socket(),
		            boost::bind(&CommandHandler::handleAccept, this,
					connection, boost::asio::placeholders::error));
}

void
CommandHandler::sendMessage(const EmsMessage& msg)
{
    std::map<uint8_t, boost::posix_time::ptime>::iterator timeIter = m_lastCommTimes.find(msg.getDestination());
    bool scheduled = false;

    if (timeIter != m_lastCommTimes.end()) {
	boost::posix_time::ptime now(boost::posix_time::microsec_clock::universal_time());
	boost::posix_time::time_duration diff = now - timeIter->second;

	if (diff.total_milliseconds() <= MinDistanceBetweenRequests) {
	    m_sendTimer.expires_at(timeIter->second + boost::posix_time::milliseconds(MinDistanceBetweenRequests));
	    m_sendTimer.async_wait(boost::bind(&CommandHandler::doSendMessage, this, msg));
	    scheduled = true;
	}
    }
    if (!scheduled) {
	doSendMessage(msg);
    }
}

void
CommandHandler::doSendMessage(const EmsMessage& msg)
{
    m_handler.sendMessage(msg);
    m_lastCommTimes[msg.getDestination()] = boost::posix_time::microsec_clock::universal_time();
}


CommandConnection::CommandConnection(CommandHandler& handler) :
    m_socket(handler.getHandler()),
    m_handler(handler),
    m_waitingForResponse(false),
    m_responseTimeout(handler.getHandler()),
    m_responseCounter(0),
    m_parsePosition(0)
{
}

CommandConnection::~CommandConnection()
{
    m_responseTimeout.cancel();
}

void
CommandConnection::handleRequest(const boost::system::error_code& error)
{
    if (error) {
	if (error != boost::asio::error::operation_aborted) {
	    m_handler.stopConnection(shared_from_this());
	}
	return;
    }

    std::istream requestStream(&m_request);

    if (m_waitingForResponse) {
	respond("ERRBUSY");
    } else if (m_request.size() > 2) {
	CommandResult result = handleCommand(requestStream);

	switch (result) {
	    case Ok:
		break;
	    case InvalidCmd:
		respond("ERRCMD");
		break;
	    case InvalidArgs:
		respond("ERRARGS");
		break;
	}
    }

    /* drain remainder */
    std::string remainder;
    std::getline(requestStream, remainder);

    startRead();
}

void
CommandConnection::handleWrite(const boost::system::error_code& error)
{
    if (error && error != boost::asio::error::operation_aborted) {
	m_handler.stopConnection(shared_from_this());
    }
}

CommandConnection::CommandResult
CommandConnection::handleCommand(std::istream& request)
{
    std::string category;
    request >> category;

    if (category == "help") {
	respond("Available commands (help with '<command> help'):\nhk[1|2|3|4]\nuba\nrc\n");
	return Ok;
    } else if (category == "hk1") {
	return handleHkCommand(request, 61);
    } else if (category == "hk2") {
	return handleHkCommand(request, 71);
    } else if (category == "hk3") {
	return handleHkCommand(request, 81);
    } else if (category == "hk4") {
	return handleHkCommand(request, 91);
    } else if (category == "ww") {
	return handleWwCommand(request);
    } else if (category == "rc") {
	return handleRcCommand(request);
    } else if (category == "uba") {
	return handleUbaCommand(request);
    } else if (category == "getversion") {
	startRequest(EmsMessage::addressUBA, 0x02, 0, 3);
	return Ok;
    }

    return InvalidCmd;
}

CommandConnection::CommandResult
CommandConnection::handleRcCommand(std::istream& request)
{
    std::string cmd;
    request >> cmd;

    if (cmd == "help") {
	respond("Available subcommands:\n"
		"geterrors\n"
		"getcontactinfo\n"
		"setcontactinfo [1|2] <text>\n");
	return Ok;
    } else if (cmd == "getcontactinfo") {
	startRequest(EmsMessage::addressRC, 0xa4, 0, 42);
	return Ok;
    } else if (cmd == "setcontactinfo") {
	unsigned int line;
	std::ostringstream buffer;
	std::string text;

	request >> line;
	if (!request || line < 1 || line > 2) {
	    return InvalidArgs;
	}

	while (request) {
	    std::string token;
	    request >> token;
	    buffer << token << " ";
	}

	// make sure there's at least 21 characters in there
	buffer << "                     ";

	text = buffer.str().substr(0, 21);
	sendCommand(EmsMessage::addressRC, 0xa4, line, (uint8_t *) text.c_str(), 21);
	return Ok;
    } else if (cmd == "geterrors") {
	startRequest(EmsMessage::addressRC, 0x12, 0, 4 * sizeof(EmsMessage::ErrorRecord));
	return Ok;
    }

    return InvalidCmd;
}

CommandConnection::CommandResult
CommandConnection::handleUbaCommand(std::istream& request)
{
    std::string cmd;
    request >> cmd;

    if (cmd == "help") {
	respond("Available subcommands:\n"
		"antipendel <minutes>\n"
		"hyst [on|off] <kelvin>\n"
		"pumpmodulation <minpercent> <maxpercent>\n"
		"pumpdelay <minutes>\n"
		"geterrors\n");
	return Ok;
    } else if (cmd == "geterrors") {
	startRequest(EmsMessage::addressUBA, 0x10, 0, 8 * sizeof(EmsMessage::ErrorRecord));
	return Ok;
    } else if (cmd == "antipendel") {
	uint8_t minutes;
	if (!parseIntParameter(request, minutes, 120)) {
	    return InvalidArgs;
	}
	sendCommand(EmsMessage::addressUBA, 0x16, 6, &minutes, 1);
	return Ok;
    } else if (cmd == "hyst") {
	std::string direction;
	uint8_t hyst;

	request >> direction;
	if (!request || (direction != "on" && direction != "off") || !parseIntParameter(request, hyst, 20)) {
	    return InvalidArgs;
	}

	sendCommand(EmsMessage::addressUBA, 0x16, direction == "on" ? 5 : 4, &hyst, 1);
	return Ok;
    } else if (cmd == "pumpmodulation") {
	unsigned int min, max;
	uint8_t data[2];

	request >> min >> max;
	if (!request || min > max || max > 100) {
	    return InvalidArgs;
	}

	data[0] = max;
	data[1] = min;

	sendCommand(EmsMessage::addressUBA, 0x16, 9, data, sizeof(data));
	return Ok;
    } else if (cmd == "pumpdelay") {
	uint8_t minutes;
	if (!parseIntParameter(request, minutes, 120)) {
	    return InvalidArgs;
	}
	sendCommand(EmsMessage::addressUBA, 0x16, 8, &minutes, 1);
	return Ok;
    }

    return InvalidCmd;
}

CommandConnection::CommandResult
CommandConnection::handleHkCommand(std::istream& request, uint8_t type)
{
    std::string cmd;
    request >> cmd;

    if (cmd == "help") {
	respond("Available subcommands:\n"
		"mode [day|night|auto]\n"
		"daytemperature <temp>\n"
		"nighttemperature <temp>\n"
		"holidaytemperature <temp>\n"
		"getholiday\n"
		"holidaymode <start:YYYY-MM-DD> <end:YYYY-MM-DD>\n"
		"getvacation\n"
		"vacationmode <start:YYYY-MM-DD> <end:YYYY-MM-DD>\n"
		"partymode <hours>\n"
		"getschedule\n"
		"schedule <index> unset\n"
		"schedule <index> [MO|TU|WE|TH|FR|SA|SU] HH:MM [ON|OFF]\n");
	return Ok;
    } else if (cmd == "mode") {
	uint8_t data;
	std::string mode;

	request >> mode;

	if (mode == "day")        data = 0x01;
	else if (mode == "night") data = 0x00;
	else if (mode == "auto")  data = 0x02;
	else return InvalidArgs;

	sendCommand(EmsMessage::addressRC, type, 7, &data, 1);
	return Ok;
    } else if (cmd == "daytemperature") {
	return handleHkTemperatureCommand(request, type, 2);
    } else if (cmd == "nighttemperature") {
	return handleHkTemperatureCommand(request, type, 1);
    } else if (cmd == "holidaytemperature") {
	return handleHkTemperatureCommand(request, type, 3);
    } else if (cmd == "holidaymode") {
	return handleSetHolidayCommand(request, type + 2, 93);
    } else if (cmd == "vacationmode") {
	return handleSetHolidayCommand(request, type + 2, 87);
    } else if (cmd == "partymode") {
	uint8_t hours;
	if (!parseIntParameter(request, hours, 99)) {
	    return InvalidArgs;
	}
	sendCommand(EmsMessage::addressRC, type, 86, &hours, 1);
	return Ok;
    } else if (cmd == "schedule") {
	unsigned int index;
	EmsMessage::ScheduleEntry entry;

	request >> index;

	if (!request || index > 42 || !parseScheduleEntry(request, &entry)) {
	    return InvalidArgs;
	}

	sendCommand(EmsMessage::addressRC, type + 2,
		(index - 1) * sizeof(EmsMessage::ScheduleEntry),
		(uint8_t *) &entry, sizeof(entry));
	return Ok;
    } else if (cmd == "getschedule") {
	startRequest(EmsMessage::addressRC, type + 2, 0, 42 * sizeof(EmsMessage::ScheduleEntry));
	return Ok;
    } else if (cmd == "getvacation") {
	startRequest(EmsMessage::addressRC, type + 2, 87, 2 * sizeof(EmsMessage::HolidayEntry));
	return Ok;
    } else if (cmd == "getholiday") {
	startRequest(EmsMessage::addressRC, type + 2, 93, 2 * sizeof(EmsMessage::HolidayEntry));
	return Ok;
    }

    return InvalidCmd;
}

CommandConnection::CommandResult
CommandConnection::handleHkTemperatureCommand(std::istream& request, uint8_t type, uint8_t offset)
{
    float value;
    uint8_t valueByte;

    request >> value;
    if (!request) {
	return InvalidArgs;
    }

    try {
	valueByte = boost::numeric_cast<uint8_t>(2 * value);
	if (valueByte < 20 || valueByte > 60) {
	    return InvalidArgs;
	}
    } catch (boost::numeric::bad_numeric_cast& e) {
	return InvalidArgs;
    }

    sendCommand(EmsMessage::addressRC, type, offset, &valueByte, 1);
    return Ok;
}

CommandConnection::CommandResult
CommandConnection::handleSetHolidayCommand(std::istream& request, uint8_t type, uint8_t offset)
{
    std::string beginString, endString;
    EmsMessage::HolidayEntry entries[2];
    EmsMessage::HolidayEntry *begin = entries;
    EmsMessage::HolidayEntry *end = entries + 1;

    request >> beginString;
    request >> endString;

    if (!request) {
	return InvalidArgs;
    }

    if (!parseHolidayEntry(beginString, begin) || !parseHolidayEntry(endString, end)) {
	return InvalidArgs;
    }

    /* make sure begin is not later than end */
    if (begin->year > end->year) {
	return InvalidArgs;
    } else if (begin->year == end->year) {
	if (begin->month > end->month) {
	    return InvalidArgs;
	} else if (begin->month == end->month) {
	    if (begin->day > end->day) {
		return InvalidArgs;
	    }
	}
    }

    sendCommand(EmsMessage::addressRC, type, offset, (uint8_t *) entries, sizeof(entries));
    return Ok;
}

CommandConnection::CommandResult
CommandConnection::handleWwCommand(std::istream& request)
{
    std::string cmd;
    request >> cmd;

    if (cmd == "help") {
	respond("Available subcommands:\n"
		"temperature <temp>\n"
		"limittemperature <temp>\n"
		"loadonce\n"
		"cancelload\n"
		"getschedule\n"
		"schedule <index> unset\n"
		"schedule <index> [MO|TU|WE|TH|FR|SA|SU] HH:MM [ON|OFF]\n"
		"selectschedule [custom|hk]\n"
		"showloadindicator [on|off]\n"
		"thermdesinfect mode [on|off]\n"
		"thermdesinfect day [monday|tuesday|wednesday|thursday|friday|saturday|sunday]\n"
		"thermdesinfect hour <hour>\n"
		"thermdesinfect temperature <temp>\n"
		"zirkpump mode [on|off|auto]\n"
		"zirkpump count [1|2|3|4|5|6|alwayson]\n"
		"zirkpump getschedule\n"
		"zirkpump schedule <index> unset\n"
		"zirkpump schedule <index> [MO|TU|WE|TH|FR|SA|SU] HH:MM [ON|OFF]\n"
		"zirkpump selectschedule [custom|hk]\n");
	return Ok;
    } else if (cmd == "thermdesinfect") {
	return handleThermDesinfectCommand(request);
    } else if (cmd == "zirkpump") {
	return handleZirkPumpCommand(request);
    } else if (cmd == "mode") {
	uint8_t data;
	std::string mode;

	request >> mode;

	if (mode == "on")        data = 0x01;
	else if (mode == "off")  data = 0x00;
	else if (mode == "auto") data = 0x02;
	else return InvalidArgs;

	sendCommand(EmsMessage::addressRC, 0x37, 2, &data, 1);
	return Ok;
    } else if (cmd == "temperature") {
	uint8_t temperature;
	if (!parseIntParameter(request, temperature, 80) || temperature < 30) {
	    return InvalidArgs;
	}
	sendCommand(EmsMessage::addressUBA, 0x33, 2, &temperature, 1);
	return Ok;
    } else if (cmd == "limittemperature") {
	uint8_t temperature;
	if (!parseIntParameter(request, temperature, 80) || temperature < 30) {
	    return InvalidArgs;
	}
	sendCommand(EmsMessage::addressRC, 0x37, 8, &temperature, 1);
	return Ok;
    } else if (cmd == "loadonce") {
	uint8_t data = 35;
	sendCommand(EmsMessage::addressUBA, 0x35, 0, &data, 1);
	return Ok;
    } else if (cmd == "cancelload") {
	uint8_t data = 3;
	sendCommand(EmsMessage::addressUBA, 0x35, 0, &data, 1);
	return Ok;
    } else if (cmd == "showloadindicator") {
	uint8_t data;
	std::string mode;

	request >> mode;

	if (mode == "on")       data = 0xff;
	else if (mode == "off") data = 0x00;
	else return InvalidArgs;

	sendCommand(EmsMessage::addressRC, 0x37, 9, &data, 1);
	return Ok;
    } else if (cmd == "getschedule") {
	startRequest(EmsMessage::addressRC, 0x38, 0, 42 * sizeof(EmsMessage::ScheduleEntry));
	return Ok;
    } else if (cmd == "schedule") {
	unsigned int index;
	EmsMessage::ScheduleEntry entry;

	request >> index;

	if (!request || index > 42 || !parseScheduleEntry(request, &entry)) {
	    return InvalidArgs;
	}

	sendCommand(EmsMessage::addressRC, 0x38,
		(index - 1) * sizeof(EmsMessage::ScheduleEntry),
		(uint8_t *) &entry, sizeof(entry));
	return Ok;
    } else if (cmd == "selectschedule") {
	std::string schedule;
	uint8_t data;

	request >> schedule;

	if (schedule == "custom")  data = 0xff;
	else if (schedule == "hk") data = 0x00;
	else return InvalidArgs;

	sendCommand(EmsMessage::addressRC, 0x37, 0, &data, 1);
	return Ok;
    }

    return InvalidCmd;
}

CommandConnection::CommandResult
CommandConnection::handleThermDesinfectCommand(std::istream& request)
{
    std::string cmd;
    request >> cmd;

    if (cmd == "mode") {
	uint8_t data;
	std::string mode;

	request >> mode;

	if (mode == "on")       data = 0xff;
	else if (mode == "off") data = 0x00;
	else return InvalidArgs;

	sendCommand(EmsMessage::addressRC, 0x37, 4, &data, 1);
	return Ok;
    } else if (cmd == "day") {
	uint8_t data;
	std::string day;

	request >> day;

	if (day == "monday")         data = 0x00;
	else if (day == "tuesday")   data = 0x01;
	else if (day == "wednesday") data = 0x02;
	else if (day == "thursday")  data = 0x03;
	else if (day == "friday")    data = 0x04;
	else if (day == "saturday")  data = 0x05;
	else if (day == "sunday")    data = 0x06;
	else if (day == "everyday")  data = 0x07;
	else return InvalidArgs;

	sendCommand(EmsMessage::addressRC, 0x37, 5, &data, 1);
	return Ok;
    } else if (cmd == "hour") {
	uint8_t hour;
	if (!parseIntParameter(request, hour, 23)) {
	    return InvalidArgs;
	}
	sendCommand(EmsMessage::addressRC, 0x37, 6, &hour, 1);
	return Ok;
    } else if (cmd == "temperature") {
	uint8_t temperature;
	if (!parseIntParameter(request, temperature, 80) || temperature < 60) {
	    return InvalidArgs;
	}
	sendCommand(EmsMessage::addressUBA, 0x33, 8, &temperature, 1);
	return Ok;
    }

    return InvalidCmd;
}

CommandConnection::CommandResult
CommandConnection::handleZirkPumpCommand(std::istream& request)
{
    std::string cmd;
    request >> cmd;

    if (cmd == "mode") {
	uint8_t data;
	std::string mode;

	request >> mode;

	if (mode == "on")        data = 0x01;
	else if (mode == "off")  data = 0x00;
	else if (mode == "auto") data = 0x02;
	else return InvalidArgs;

	sendCommand(EmsMessage::addressRC, 0x37, 3, &data, 1);
	return Ok;
    } else if (cmd == "count") {
	uint8_t count;
	std::string countString;

	request >> countString;

	if (countString == "alwayson") {
	    count = 0x07;
	} else {
	    try {
		count = boost::lexical_cast<unsigned int>(countString);
		if (count < 1 || count > 6) {
		    return InvalidArgs;
		}
	    } catch (boost::bad_lexical_cast& e) {
		return InvalidArgs;
	    }
	}
	sendCommand(EmsMessage::addressUBA, 0x33, 7, &count, 1);
	return Ok;
    } else if (cmd == "getschedule") {
	startRequest(EmsMessage::addressRC, 0x39, 0, 42 * sizeof(EmsMessage::ScheduleEntry));
	return Ok;
    } else if (cmd == "schedule") {
	unsigned int index;
	EmsMessage::ScheduleEntry entry;

	request >> index;

	if (!request || index > 42 || !parseScheduleEntry(request, &entry)) {
	    return InvalidArgs;
	}

	sendCommand(EmsMessage::addressRC, 0x39,
		(index - 1) * sizeof(EmsMessage::ScheduleEntry),
		(uint8_t *) &entry, sizeof(entry));
	return Ok;
    } else if (cmd == "selectschedule") {
	std::string schedule;
	uint8_t data;

	request >> schedule;

	if (schedule == "custom")  data = 0xff;
	else if (schedule == "hk") data = 0x00;
	else return InvalidArgs;

	sendCommand(EmsMessage::addressRC, 0x37, 1,  &data, 1);
	return Ok;
    }

    return InvalidCmd;
}

void
CommandConnection::handlePcMessage(const EmsMessage& message)
{
    if (!m_waitingForResponse) {
	return;
    }

    const std::vector<uint8_t>& data = message.getData();
    uint8_t source = message.getSource();
    uint8_t type = message.getType();

    if (type == 0xff) {
	m_waitingForResponse = false;
	respond(data[0] == 0x04 ? "FAIL" : "OK");
	return;
    }

    m_responseTimeout.cancel();
    m_requestResponse.insert(m_requestResponse.end(), data.begin() + 1, data.end());

    bool done = false;

    switch (type) {
	case 0x02: /* get version */ {
	    static const struct {
		uint8_t source;
		const char *name;
	    } SOURCES[] = {
		{ EmsMessage::addressUBA, "UBA" },
		{ EmsMessage::addressBC10, "BC10" },
		{ EmsMessage::addressRC, "RC" }
	    };
	    static const size_t SOURCECOUNT = sizeof(SOURCES) / sizeof(SOURCES[0]);

	    unsigned int major = data[2];
	    unsigned int minor = data[3];
	    size_t index;

	    for (index = 0; index < SOURCECOUNT; index++) {
		if (source == SOURCES[index].source) {
		    std::ostringstream os;
		    os << SOURCES[index].name << " version: ";
		    os << major << "." << std::setw(2) << std::setfill('0') << minor;
		    respond(os.str());
		    break;
		}
	    }
	    if (index < (SOURCECOUNT - 1)) {
		startRequest(SOURCES[index + 1].source, 0x02, 0, 3);
	    } else {
		done = true;
	    }
	    break;
	}
	case 0x10: /* get UBA errors */
	case 0x11: /* get UBA errors 2 */
	case 0x12: /* get RC errors */
	case 0x13: /* get RC errors 2 */ {
	    const char *prefix = type == 0x12 ? "S" : type == 0x11 ? "L" : "B";
	    done = loopOverResponse<EmsMessage::ErrorRecord>(prefix);
	    if (!done) {
		done = !continueRequest();
		if (done && (type == 0x10 || type == 0x12)) {
		    unsigned int count = type == 0x10 ? 5 : 4;
		    startRequest(source, type + 1, 0, count * sizeof(EmsMessage::ErrorRecord), false);
		    done = false;
		}
	    }
	    break;
	}
	case 0x3f: /* get schedule HK1 */
	case 0x49: /* get schedule HK2 */
	case 0x53: /* get schedule HK3 */
	case 0x5d: /* get schedule HK4 */
	    if (data[0] > 80) {
		/* it's at the end -> holiday schedule */
		const size_t msgSize = sizeof(EmsMessage::HolidayEntry);

		if (m_requestResponse.size() >= 2 * msgSize) {
		    EmsMessage::HolidayEntry *begin = (EmsMessage::HolidayEntry *) &m_requestResponse.at(0);
		    EmsMessage::HolidayEntry *end = (EmsMessage::HolidayEntry *) &m_requestResponse.at(msgSize);
		    respond(buildRecordResponse("BEGIN", begin));
		    respond(buildRecordResponse("END", end));
		    done = true;
		} else {
		    respond("FAIL");
		}
	    } else {
		/* it's at the beginning -> heating schedule */
		done = loopOverResponse<EmsMessage::ScheduleEntry>();
		if (!done) {
		    done = !continueRequest();
		}
	    }
	    break;
	case 0x38: /* get WW schedule */
	case 0x39: /* get WW ZP schedule */
	    done = loopOverResponse<EmsMessage::ScheduleEntry>();
	    if (!done) {
		done = !continueRequest();
	    }
	    break;
	case 0xa4: { /* get contact info */
	    // RC30 doesn't support this and always returns empty responses
	    done = !continueRequest() || data.size() == 1;
	    if (done) {
		for (size_t i = 1; i < data.size(); i += 21) {
		    size_t len = std::min(data.size() - i, static_cast<size_t>(21));
		    char buffer[22];
		    memcpy(buffer, &data.at(i), len);
		    buffer[len] = 0;
		    respond(buffer);
		}
	    }
	    break;
	}
    }

    if (done) {
	m_waitingForResponse = false;
	respond("OK");
    }
}

template<typename T> bool
CommandConnection::loopOverResponse(const char *prefix)
{
    const size_t msgSize = sizeof(T);
    while (m_parsePosition + msgSize <= m_requestResponse.size()) {
	T *record = (T *) &m_requestResponse.at(m_parsePosition);
	std::string response = buildRecordResponse(record);

	m_parsePosition += msgSize;
	m_responseCounter++;

	if (response.empty()) {
	    return true;
	}

	std::ostringstream os;
	os << prefix << std::setw(2) << std::setfill('0') << m_responseCounter << " " << response;
	respond(os.str());
    }

    return false;
}

void
CommandConnection::scheduleResponseTimeout()
{
    m_waitingForResponse = true;
    m_responseTimeout.expires_from_now(boost::posix_time::seconds(2));
    m_responseTimeout.async_wait(boost::bind(&CommandConnection::responseTimeout,
					     this, boost::asio::placeholders::error));
}

void
CommandConnection::responseTimeout(const boost::system::error_code& error)
{
    if (m_waitingForResponse && error != boost::asio::error::operation_aborted) {
	respond("ERRTIMEOUT");
	m_waitingForResponse = false;
    }
}

std::string
CommandConnection::buildRecordResponse(const EmsMessage::ErrorRecord *record)
{
    if (record->errorAscii[0] == 0) {
	/* no error at this position */
	return "";
    }

    std::ostringstream response;

    if (record->hasDate) {
	response << std::setw(4) << (unsigned int) (2000 + record->year) << "-";
	response << std::setw(2) << std::setfill('0') << (unsigned int) record->month << "-";
	response << std::setw(2) << std::setfill('0') << (unsigned int) record->day << " ";
	response << std::setw(2) << std::setfill('0') << (unsigned int) record->hour << ":";
	response << std::setw(2) << std::setfill('0') << (unsigned int) record->minute;
    } else {
	response  << "xxxx-xx-xx xx:xx";
    }

    response << " ";
    response << std::hex << (unsigned int) record->source << " ";

    response << std::dec << record->errorAscii[0] << record->errorAscii[1] << " ";
    response << __be16_to_cpu(record->code_be16) << " ";
    response << __be16_to_cpu(record->durationMinutes_be16);

    return response.str();
}

static const char * dayNames[] = {
    "MO", "TU", "WE", "TH", "FR", "SA", "SU"
};

std::string
CommandConnection::buildRecordResponse(const EmsMessage::ScheduleEntry *entry)
{
    if (entry->time >= 0x90) {
	/* unset */
	return "";
    }

    std::ostringstream response;
    unsigned int minutes = entry->time * 10;
    response << dayNames[entry->day / 2] << " ";
    response << std::setw(2) << std::setfill('0') << (minutes / 60) << ":";
    response << std::setw(2) << std::setfill('0') << (minutes % 60) << " ";
    response << (entry->on ? "ON" : "OFF");

    return response.str();
}

bool
CommandConnection::parseScheduleEntry(std::istream& request, EmsMessage::ScheduleEntry *entry)
{
    std::string day, time, mode;

    request >> day;
    if (!request) {
	return false;
    }

    if (day == "unset") {
	entry->on = 7;
	entry->day = 0xe;
	entry->time = 0x90;
	return true;
    }

    request >> time >> mode;
    if (!request) {
	return false;
    }

    if (mode == "ON") {
	entry->on = 1;
    } else if (mode == "OFF") {
	entry->on = 0;
    } else {
	return false;
    }

    bool hasDay = false;
    for (size_t i = 0; i < sizeof(dayNames) / sizeof(dayNames[0]); i++) {
	if (day == dayNames[i]) {
	    entry->day = 2 * i;
	    hasDay = true;
	    break;
	}
    }
    if (!hasDay) {
	return false;
    }

    size_t pos = time.find(":");
    if (pos == std::string::npos) {
	return false;
    }
    unsigned int hours = boost::lexical_cast<unsigned int>(time.substr(0, pos));
    unsigned int minutes = boost::lexical_cast<unsigned int>(time.substr(pos + 1));
    if (hours > 23 || minutes >= 60 || (minutes % 10) != 0) {
	return false;
    }

    entry->time = (uint8_t) ((hours * 60 + minutes) / 10);

    return true;
}

std::string
CommandConnection::buildRecordResponse(const char *type, const EmsMessage::HolidayEntry *entry)
{
    std::ostringstream response;

    response << type << " ";
    response << std::setw(2) << std::setfill('0') << (unsigned int) entry->day << "-";
    response << std::setw(2) << std::setfill('0') << (unsigned int) entry->month << "-";
    response << std::setw(4) << (unsigned int) (2000 + entry->year);

    return response.str();
}

bool
CommandConnection::parseHolidayEntry(const std::string& string, EmsMessage::HolidayEntry *entry)
{
    size_t pos = string.find('-');
    if (pos == std::string::npos) {
	return false;
    }

    size_t pos2 = string.find('-', pos + 1);
    if (pos2 == std::string::npos) {
	return false;
    }

    unsigned int year = boost::lexical_cast<unsigned int>(string.substr(0, pos));
    unsigned int month = boost::lexical_cast<unsigned int>(string.substr(pos + 1, pos2 - pos - 1));
    unsigned int day = boost::lexical_cast<unsigned int>(string.substr(pos2 + 1));
    if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
	return false;
    }

    entry->year = (uint8_t) (year - 2000);
    entry->month = (uint8_t) month;
    entry->day = (uint8_t) day;

    return true;
}

void
CommandConnection::startRequest(uint8_t dest, uint8_t type, size_t offset,
			        size_t length, bool newRequest)
{
    m_requestOffset = offset;
    m_requestLength = length;
    m_requestDestination = dest;
    m_requestType = type;
    m_requestResponse.clear();
    m_requestResponse.reserve(length);
    m_parsePosition = 0;
    if (newRequest) {
	m_responseCounter = 0;
    }

    continueRequest();
}

bool
CommandConnection::continueRequest()
{
    size_t alreadyReceived = m_requestResponse.size();

    if (alreadyReceived >= m_requestLength) {
	return false;
    }

    uint8_t offset = (uint8_t) (m_requestOffset + alreadyReceived);
    uint8_t remaining = (uint8_t) (m_requestLength - alreadyReceived);

    sendCommand(m_requestDestination, m_requestType, offset, &remaining, 1, true);
    return true;
}

void
CommandConnection::sendCommand(uint8_t dest, uint8_t type, uint8_t offset,
			       const uint8_t *data, size_t count,
			       bool expectResponse)
{
    std::vector<uint8_t> sendData(data, data + count);
    sendData.insert(sendData.begin(), offset);

    scheduleResponseTimeout();

    EmsMessage msg(dest, type, sendData, expectResponse);
    m_handler.sendMessage(msg);
}

bool
CommandConnection::parseIntParameter(std::istream& request, uint8_t& data, uint8_t max)
{
    unsigned int value;

    request >> value;
    if (!request || value > max) {
	return false;
    }

    data = value;
    return true;
}
