#include "config.h"

#include "Poco/Util/WinRegistryConfiguration.h"
#include "poco/Path.h"
#include <Poco/Data/Session.h>
#include <boost/lexical_cast.hpp>

using namespace Poco;
using namespace Poco::Util;

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/ofstd/ofstd.h"
#include "dcmtk/oflog/oflog.h"
#include "dcmtk/dcmdata/dctk.h"
#include "dcmtk/dcmimgle/dcmimage.h"
#include "dcmtk/dcmimage/diregist.h" // register support for color

#include "dcmtk/dcmdata/dcrledrg.h"	// rle decoder
#include "dcmtk/dcmjpeg/djdecode.h"	// jpeg decoder
#include "dcmtk/dcmjpeg/djencode.h"
#include "dcmtk/dcmjpls/djdecode.h"	// jpeg-ls decoder
#include "dcmtk/dcmjpls/djencode.h"
#include "dcmtk/dcmdata/dcrledrg.h"
#include "dcmtk/dcmdata/dcrleerg.h"
#include "fmjpeg2k/djencode.h"
#include "fmjpeg2k/djdecode.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

boost::filesystem::path config::getTempPath()
{
	AutoPtr<WinRegistryConfiguration> pConf(new WinRegistryConfiguration("HKEY_LOCAL_MACHINE\\SOFTWARE\\FrontMotion\\fmdscp"));

	std::string p = pConf->getString("TempPath", boost::filesystem::temp_directory_path().string());

	return Path::expand(p);
}

boost::filesystem::path config::getStoragePath()
{
	AutoPtr<WinRegistryConfiguration> pConf(new WinRegistryConfiguration("HKEY_LOCAL_MACHINE\\SOFTWARE\\FrontMotion\\fmdscp"));

	std::string p = pConf->getString("StoragePath", "C:\\PACS\\Storage");
	return Path::expand(p);
}

int config::getDICOMListeningPort()
{
	AutoPtr<WinRegistryConfiguration> pConf(new WinRegistryConfiguration("HKEY_LOCAL_MACHINE\\SOFTWARE\\FrontMotion\\fmdscp"));

	return pConf->getInt("DICOMListeningPort", 104);
}

std::string config::getFrontEnd()
{
	AutoPtr<WinRegistryConfiguration> pConf(new WinRegistryConfiguration("HKEY_LOCAL_MACHINE\\SOFTWARE\\FrontMotion\\fmdscp"));
	std::string host = pConf->getString("FrontEndHost", "localhost");
	int port = pConf->getInt("FrontEndPort", 3000);

	std::string frontend = "http://" + host + ":" + boost::lexical_cast<std::string>(port);
	return frontend;
}

void config::registerCodecs()
{
	DJEncoderRegistration::registerCodecs();
	DJDecoderRegistration::registerCodecs();
	DJLSEncoderRegistration::registerCodecs();
	DJLSDecoderRegistration::registerCodecs();
	DcmRLEEncoderRegistration::registerCodecs();
	DcmRLEDecoderRegistration::registerCodecs();
	FMJPEG2KEncoderRegistration::registerCodecs();
	FMJPEG2KDecoderRegistration::registerCodecs();	
}


void config::deregisterCodecs()
{
	DJEncoderRegistration::cleanup();
	DJDecoderRegistration::cleanup();
	DJLSEncoderRegistration::cleanup();
	DJLSDecoderRegistration::cleanup();
	DcmRLEEncoderRegistration::cleanup();
	DcmRLEDecoderRegistration::cleanup();
	FMJPEG2KEncoderRegistration::cleanup();
	FMJPEG2KDecoderRegistration::cleanup();	
}

std::string config::getConnectionString()
{
	AutoPtr<WinRegistryConfiguration> pConf(new WinRegistryConfiguration("HKEY_LOCAL_MACHINE\\SOFTWARE\\FrontMotion\\fmdscp"));

	return pConf->getString("ConnectionString", "host=localhost;port=3306;user=root;password=root;db=pacs");
}

bool config::test(std::string &errormsg, DBPool &dbpool)
{
	bool result = true;

	try
	{
		// test database connection
		Poco::Data::Session dbconnection(dbpool.get());
	}	
	catch(std::exception &e)
	{
		errormsg = "database connection not working: ";
		errormsg += e.what();
		errormsg += ", try host=<host>;port=3306;user=root;password=<password>;db=pacsdb_dev";
		result = false;
	}

	if(!result)
		return false;

	try
	{
		// test to see if we can write to the temp dir
		boost::filesystem::path tempTest = config::getTempPath();
		tempTest /= "_test";

		if(!boost::filesystem::create_directories(tempTest))
			return false;

		boost::filesystem::remove(tempTest);

		// test to see if we can write to the storage dir
		boost::filesystem::path storageTest = config::getTempPath();
		storageTest /= "_test";

		if(!boost::filesystem::create_directories(storageTest))
			return false;

		boost::filesystem::remove(storageTest);
	}
	catch(std::exception &e)
	{
		errormsg = e.what();
		result = false;
	}

	return result;
}
