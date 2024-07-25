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

    typedef void(__thiscall *StandardOutRaised_t)(void *, RBX::StandardOutMessage);

    extern StandardOutRaised_t StandardOutRaised;
}