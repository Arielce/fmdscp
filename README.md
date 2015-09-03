# fmdscp
Simple DICOM SCP

- Available on Windows, OS X, and Linux
- Supports Unicode file and path.
- No dll's need to be distributed.
- Native, no Java required.

## Download
Binary http://www.draconpern.com/software/fmdscp
Source https://github.com/DraconPern/fmdscp

## Development notes
The program is http://utf8everywhere.org/

## Requirements
- CMake http://www.cmake.org/download/
- XCode on OS X
- Visual Studio 2012 or higher on Windows
- gcc on Linux

## Third party dependency
- poco http://pocoproject.org please extract under ./poco
- DCMTK http://dicom.offis.de/ please use snapshot or git, and extract under ./dcmtk
- boost http://www.boost.org/ please extract under ./boost
- Visual Leak Detector https://vld.codeplex.com/ installed for debug release
- zlib please extract under ./zlib
- openjpeg http://www.openjpeg.org please extract under ./openjpeg
- fmjpeg2koj https://github.com/DraconPern/fmjpeg2koj please extract under ./fmjpeg2koj

## Author
Ing-Long Eric Kuo <draconpern@hotmail.com>

## License
This software is licensed under the GPL.  For use under another license, please contact Ing-Long Eric Kuo <eric@frontmotion.com>