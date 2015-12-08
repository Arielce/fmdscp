﻿#include <boost/algorithm/string.hpp>
#include <set>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "boost/date_time/local_time/local_time.hpp"
#include <codecvt>

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include <winsock2.h>	// include winsock2 before network includes
#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmdata/dctk.h"
#include "dcmtk/dcmnet/diutil.h"
#include "dcmtk/oflog/ndc.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include "store.h"

#include "poco/Data/Session.h"
using namespace Poco::Data::Keywords;

#include "model.h"
#include "config.h"
#include "util.h"

using namespace boost::gregorian;
using namespace boost::posix_time;
using namespace boost::local_time;

OFCondition StoreHandler::handleSTORERequest(boost::filesystem::path filename)
{
	OFCondition status = EC_IllegalParameter;

	DcmFileFormat dfile;
	OFCondition cond = dfile.loadFile(filename.c_str());

	OFString sopuid, seriesuid, studyuid;
	dfile.getDataset()->findAndGetOFString(DCM_StudyInstanceUID, studyuid);
	dfile.getDataset()->findAndGetOFString(DCM_SeriesInstanceUID, seriesuid);
	dfile.getDataset()->findAndGetOFString(DCM_SOPInstanceUID, sopuid);

	if (studyuid.length() == 0)
	{
		// DEBUGLOG(sessionguid, DB_ERROR, L"No Study UID\r\n");
		return status;
	}
	if (seriesuid.length() == 0)
	{
		// DEBUGLOG(sessionguid, DB_ERROR, L"No Series UID\r\n");
		return status;
	}
	if (sopuid.length() == 0)
	{
		// DEBUGLOG(sessionguid, DB_ERROR, L"No SOP UID\r\n");
		return status;
	}

	boost::filesystem::path newpath = config::getStoragePath();
	newpath /= studyuid.c_str();
	newpath /= seriesuid.c_str();
	boost::filesystem::create_directories(newpath);
	newpath /= std::string(sopuid.c_str()) + ".dcm";

	
	std::stringstream msg;
#ifdef _WIN32
	// on Windows, boost::filesystem::path is a wstring, so we need to convert to utf8
	msg << "Saving file: " << newpath.string(std::codecvt_utf8<boost::filesystem::path::value_type>());
#else
	msg << "Saving file: " << newpath.string();
#endif
	DCMNET_INFO(msg.str());


	dfile.getDataset()->chooseRepresentation(EXS_JPEGLSLossless, NULL);
	if(dfile.getDataset()->canWriteXfer(EXS_JPEGLSLossless))
	{
		dfile.getDataset()->loadAllDataIntoMemory();

		dfile.saveFile(newpath.string().c_str(), EXS_JPEGLSLossless);

		DCMNET_INFO("Changed to JPEG LS lossless");
	}
	else
	{
		boost::filesystem::copy(filename, newpath);

		DCMNET_INFO("Copied");
	}

	// now try to add the file into the database
	AddDICOMFileInfoToDatabase(newpath);

	// delete the temp file
	boost::filesystem::remove(filename);

	status = EC_Normal;

	return status;
}

bool StoreHandler::AddDICOMFileInfoToDatabase(boost::filesystem::path filename)
{
	// now try to add the file into the database
	if(!boost::filesystem::exists(filename))
		return false;

	DcmFileFormat dfile;
	if(dfile.loadFile(filename.c_str()).bad())
		return false;

	OFDate datebuf;
	OFTime timebuf;
	OFString textbuf;
	long numberbuf;

	std::string sopuid, seriesuid, studyuid;
	dfile.getDataset()->findAndGetOFString(DCM_SOPInstanceUID, textbuf);
	sopuid = textbuf.c_str();
	dfile.getDataset()->findAndGetOFString(DCM_SeriesInstanceUID, textbuf);
	seriesuid = textbuf.c_str();
	dfile.getDataset()->findAndGetOFString(DCM_StudyInstanceUID, textbuf);
	studyuid = textbuf.c_str();

	bool isOk = true;
	try
	{

		Poco::Data::Session dbconnection(config::getConnectionString());		

		//		if(cbdata->last_studyuid != studyuid)
		{
			
			std::vector<PatientStudy> patientstudies;
			Poco::Data::Statement patientstudiesselect(dbconnection);
			patientstudiesselect << "SELECT id,"
				"StudyInstanceUID,"
				"StudyID,"
				"AccessionNumber,"
				"PatientName,"
				"PatientID,"
				"StudyDate,"
				"ModalitiesInStudy,"
				"StudyDescription,"
				"PatientSex,"
				"PatientBirthDate,"
				"ReferringPhysicianName,"
				"created_at,updated_at"
				" FROM patient_studies WHERE StudyInstanceUID = ?",
				into(patientstudies),
				use(studyuid);

			patientstudiesselect.execute();

			if(patientstudies.size() == 0)
			{
				// insert
				patientstudies.push_back(PatientStudy());
			}
			
			PatientStudy &patientstudy = patientstudies[0];

			patientstudy.StudyInstanceUID = studyuid;

			dfile.getDataset()->findAndGetOFString(DCM_PatientName, textbuf);
			patientstudy.PatientName = textbuf.c_str();
			
			dfile.getDataset()->findAndGetOFString(DCM_StudyID, textbuf);
			patientstudy.StudyID = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_AccessionNumber, textbuf);
			patientstudy.AccessionNumber = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_PatientID, textbuf);
			patientstudy.PatientID = textbuf.c_str();

			datebuf = getDate(dfile.getDataset(), DCM_StudyDate);
			timebuf = getTime(dfile.getDataset(), DCM_StudyTime);
			// use DCM_TimezoneOffsetFromUTC ?
			if(datebuf.isValid() && timebuf.isValid()) 
			{			
				patientstudy.StudyDate.assign(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay(), timebuf.getHour(), timebuf.getMinute(), timebuf.getSecond());
			}

			// handle modality list...
			std::string modalitiesinstudy = patientstudy.ModalitiesInStudy;
			std::set<std::string> modalityarray;
			if(modalitiesinstudy.length() > 0)
				boost::split(modalityarray, modalitiesinstudy, boost::is_any_of("\\"));
			dfile.getDataset()->findAndGetOFStringArray(DCM_Modality, textbuf);
			modalityarray.insert(textbuf.c_str());
			patientstudy.ModalitiesInStudy = boost::join(modalityarray, "\\");

			dfile.getDataset()->findAndGetOFString(DCM_StudyDescription, textbuf);
			patientstudy.StudyDescription = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_PatientSex, textbuf);
			patientstudy.PatientSex = textbuf.c_str();

			datebuf = getDate(dfile.getDataset(), DCM_PatientBirthDate);
			if(datebuf.isValid()) 
				patientstudy.PatientBirthDate.assign(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay());

			dfile.getDataset()->findAndGetOFString(DCM_ReferringPhysicianName, textbuf);
			patientstudy.ReferringPhysicianName = textbuf.c_str();

			patientstudy.updated_at = Poco::DateTime();

			if(patientstudy.id != 0)
			{				
				Poco::Data::Statement update(dbconnection);
				update << "UPDATE patient_studies SET "
					"id = ?,"
					"StudyInstanceUID = ?,"
					"StudyID = ?,"
					"AccessionNumber = ?,"
					"PatientName = ?,"
					"PatientID = ?,"
					"StudyDate = ?,"
					"ModalitiesInStudy = ?,"
					"StudyDescription = ?,"
					"PatientSex = ?,"
					"PatientBirthDate = ?,"
					"ReferringPhysicianName = ?,"
					"created_at = ?, updated_at = ?"
					" WHERE id = ?",
					use(patientstudy),
					use(patientstudy.id);

				update.execute();
			}
			else
			{
				
				patientstudy.created_at = Poco::DateTime();

				Poco::Data::Statement insert(dbconnection);
				insert << "INSERT INTO patient_studies VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
					use(patientstudy);

				insert.execute();
			}
		}
		/*
		//if(cbdata->last_seriesuid != seriesuid)
		{
			// update the series table			
			std::vector<Series> series;
			Poco::Data::Statement seriesselect(dbconnection);
			seriesselect << "SELECT id,"
				"SeriesInstanceUID,"
				"StudyInstanceUID,"
				"Modality,"
				"SeriesDescription,"
				"SeriesNumber,"
				"SeriesDate,"
				"created_at,updated_at"
				" FROM series WHERE SeriesInstanceUID = :seriesuid",
				into(series),
				use(seriesuid);

			series.SeriesInstanceUID = seriesuid;

			dfile.getDataset()->findAndGetOFString(DCM_StudyInstanceUID, textbuf);
			series.StudyInstanceUID = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_Modality, textbuf);
			series.Modality = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_SeriesDescription, textbuf);
			series.SeriesDescription = textbuf.c_str();

			dfile.getDataset()->findAndGetSint32(DCM_SeriesNumber, numberbuf);
			series.SeriesNumber = numberbuf;

			datebuf = getDate(dfile.getDataset(), DCM_SeriesDate);
			timebuf = getTime(dfile.getDataset(), DCM_SeriesTime);
			// use DCM_TimezoneOffsetFromUTC ?
			if(datebuf.isValid() && timebuf.isValid()) 
			{
				ptime t1(date(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay()), hours(timebuf.getHour())+minutes(timebuf.getMinute())+seconds(timebuf.getSecond()));
				series.SeriesDate = to_tm(t1);
			}
			
			series.updated_at = to_tm(second_clock::universal_time());

			if(seriesselect.got_data())
			{
				soci::session &update = dbconnection;
				update << "UPDATE series SET "
					"SeriesInstanceUID = :SeriesInstanceUID,"
					"StudyInstanceUID = :StudyInstanceUID,"
					"Modality = :Modality,"
					"SeriesDescription = :SeriesDescription,"
					"SeriesNumber = :SeriesNumber,"
					"SeriesDate = :SeriesDate,"
					"created_at = :created_at, updated_at = :updated_at"
					" WHERE id = :id",
					soci::use(series);
			}
			else
			{
				series.id = 0;
				series.created_at = to_tm(second_clock::universal_time());				

				soci::session &insert = dbconnection;
				insert << "INSERT INTO series VALUES(0, :SeriesInstanceUID, :StudyInstanceUID, :Modality,"
					":SeriesDescription, :SeriesNumber,:SeriesDate,"
					":created_at, :updated_at)",
					soci::use(series);
			}
		}
		*/
		// update the images table
		/*
		Instance instance;		
		soci::session &instanceselect = dbconnection;
		instanceselect << "SELECT id,"
			"SOPInstanceUID,"
			"SeriesInstanceUID,"
			"InstanceNumber,"
			"created_at,updated_at"
			" FROM instances WHERE SOPInstanceUID = :SOPInstanceUID",
			soci::into(instance),
			soci::use(sopuid);

		instance.SOPInstanceUID = sopuid;

		dfile.getDataset()->findAndGetOFString(DCM_SeriesInstanceUID, textbuf);
		instance.SeriesInstanceUID = textbuf.c_str();

		dfile.getDataset()->findAndGetSint32(DCM_InstanceNumber, numberbuf);
		instance.InstanceNumber = numberbuf;

		instance.updated_at = to_tm(second_clock::universal_time());

		if(instanceselect.got_data())
		{
			soci::session &update = dbconnection;			
			update << "UPDATE instances SET "
				"SOPInstanceUID = :SOPInstanceUID,"
				"SeriesInstanceUID = :SeriesInstanceUID,"
				"InstanceNumber = :InstanceNumber,"
				"created_at = :created_at, updated_at = :updated_at"
				" WHERE id = :id",
				soci::use(instance);
		}
		else
		{
			instance.id = 0;
			instance.created_at = to_tm(second_clock::universal_time());				
			
			soci::session &insert = dbconnection;
			insert << "INSERT INTO instances VALUES(0, :SOPInstanceUID, :SeriesInstanceUID, :InstanceNumber,"
				":created_at, :updated_at)",
				soci::use(instance);
		}
		*/
	}
	catch(Poco::Data::DataException &e)
	{
		std::string what = e.message();
		isOk = false;
		DCMNET_ERROR(what);
	}

	return isOk;
}
