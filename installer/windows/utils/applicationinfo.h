#ifndef APPLICATIONINFO_H
#define APPLICATIONINFO_H

#include <Windows.h>
#include <string>

class ApplicationInfo
{
 public:

	static ApplicationInfo &instance()
	{
		static ApplicationInfo s;
		return s;
	}

    std::wstring getName() const;
    std::wstring getVersion() const;
	std::wstring getUninstall() const;
	std::wstring getPublisher() const;
	std::wstring getId() const;
	std::wstring getPublisherUrl() const;
	std::wstring getSupportUrl() const;
	std::wstring getUpdateUrl() const;

    bool appIsRunning();

private:
	ApplicationInfo();

	const std::wstring id = L"{fa690e90-ddb0-4f0c-b3f1-136c084e5fc7}";
	const std::wstring name = L"Windscribe";
	const std::wstring version = L"1.83 Build 18";
	const std::wstring publisher = L"Windscribe Limited";
	const std::wstring url = L"http://www.windscribe.com/";
	const std::wstring publisherUrl = L"http://www.windscribe.com/";
	const std::wstring supportURL = L"http://www.windscribe.com/";
	const std::wstring updateURL = L"http://www.windscribe.com/";
	const std::wstring uninstall = L"uninstall.exe";

	const std::wstring affiliateCpid;
	const std::wstring affiliateId;
	const std::wstring affiliateTag;

};

#endif // APPLICATION_H
