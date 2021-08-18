#ifndef IKEV2IPSEC_H
#define IKEV2IPSEC_H

class IKEv2IPSec
{
public:
    static bool setIKEv2IPSecParameters();

private:
    static bool setIKEv2IPSecParametersInPhoneBook();
    static bool setIKEv2IPSecParametersPowerShell();
};

#endif // IKEV2IPSEC_H
