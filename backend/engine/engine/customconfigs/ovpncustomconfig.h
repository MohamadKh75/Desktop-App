#ifndef OVPNCUSTOMCONFIG_H
#define OVPNCUSTOMCONFIG_H

#include <QVector>
#include "icustomconfig.h"
#include "engine/types/protocoltype.h"

namespace customconfigs {

struct RemoteCommandLine
{
    QString hostname;               // ip or hostname
    QString originalRemoteCommand;
};

class OvpnCustomConfig : public ICustomConfig
{
public:
    CUSTOM_CONFIG_TYPE type() const override;
    QString name() const override;      // use for show in the GUI, basically filename without extension
    QString filename() const override;  // filename (without full path). It is used as an identifier for the LocationID
    QStringList hostnames() const override;      // hostnames/ips

    bool isCorrect() const override;
    QString getErrorForIncorrect() const override;

    static ICustomConfig *makeFromFile(const QString &filepath);

private:
    bool isCorrect_;
    QString errMessage_;

    QString name_;
    QString filename_;
    QString filepath_;

    QString ovpnData_;  // ovpn file without "remote" commands. They are all cut out and converted to a list of structures "RemoteItem"
    QVector<RemoteCommandLine> remotes_;

    void process();

};

} //namespace customconfigs

#endif // OVPNCUSTOMCONFIG_H
