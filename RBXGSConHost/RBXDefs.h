#pragma once

namespace RBX
{
	struct StandardOutMessage 
	{
		int type;
		std::string message;
		double time;
	};

	enum MessageType
	{
		MESSAGE_OUTPUT,
		MESSAGE_INFO,
		MESSAGE_WARNING,
		MESSAGE_ERROR
	};
}