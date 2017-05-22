#pragma once
#include "stdafx.h"
#include "../Utilities/CRC32.h"
#include "../Utilities/md5.h"
#include "../Utilities/HexUtilities.h"
#include "RomData.h"
#include "GameDatabase.h"
#include "UnifBoards.h"
#include "MessageManager.h"
#include "EmulationSettings.h"
#include <unordered_map>

class UnifLoader
{
private:
	static std::unordered_map<string, int> _boardMappings;

	vector<uint8_t> _prgChunks[16];
	vector<uint8_t> _chrChunks[16];
	string _mapperName;

	void Read(uint8_t* &data, uint8_t& dest)
	{
		dest = data[0];
		data++;
	}

	void Read(uint8_t* &data, uint32_t& dest)
	{
		dest = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
		data += 4;
	}

	void Read(uint8_t* &data, uint8_t* dest, size_t len)
	{
		memcpy(dest, data, len);
		data += len;
	}

	string ReadString(uint8_t* &data, uint8_t* chunkEnd)
	{
		stringstream ss;
		while(data < chunkEnd) {
			if(data[0] == 0) {
				//end of string
				data = chunkEnd;
				break;
			} else {
				ss << (char)data[0];
			}
			data++;
		}

		return ss.str();
	}

	string ReadFourCC(uint8_t* &data)
	{
		stringstream ss;
		for(int i = 0; i < 4; i++) {
			ss << (char)data[i];
		}
		data += 4;
		return ss.str();
	}

	bool ReadChunk(uint8_t* &data, uint8_t* dataEnd, RomData& romData)
	{
		if(data + 8 > dataEnd) {
			return false;
		}

		string fourCC = ReadFourCC(data);

		uint32_t length;
		Read(data, length);

		uint8_t* chunkEnd = data + length;
		if(chunkEnd > dataEnd) {
			return false;
		}

		if(fourCC.compare("MAPR") == 0) {
			_mapperName = ReadString(data, chunkEnd);
			if(_mapperName.size() > 0) {
				romData.MapperID = GetMapperID(_mapperName);
				if(romData.MapperID == UnifBoards::UnknownBoard) {
					MessageManager::Log("[UNIF] Error: Unknown board");
				}
			} else {
				romData.Error = true;
				return false;
			}
		} else if(fourCC.substr(0, 3).compare("PRG") == 0) {
			uint32_t chunkNumber;
			std::stringstream ss;
			ss << std::hex << fourCC[3];
			ss >> chunkNumber;

			_prgChunks[chunkNumber].resize(length);
			Read(data, _prgChunks[chunkNumber].data(), length);
		} else if(fourCC.substr(0, 3).compare("CHR") == 0) {
			uint32_t chunkNumber;
			std::stringstream ss;
			ss << std::hex << fourCC[3];
			ss >> chunkNumber;

			_chrChunks[chunkNumber].resize(length);
			Read(data, _chrChunks[chunkNumber].data(), length);
		} else if(fourCC.compare("TVCI") == 0) {
			uint8_t value;
			Read(data, value);
			romData.System = value == 1 ? GameSystem::NesPal : GameSystem::NesNtsc;
		} else if(fourCC.compare("CTRL") == 0) {
			//not supported
		} else if(fourCC.compare("BATR") == 0) {
			uint8_t value;
			Read(data, value);
			romData.HasBattery = value > 0;
		} else if(fourCC.compare("MIRR") == 0) {
			uint8_t value;
			Read(data, value);

			switch(value) {
				default:
				case 0: romData.Mirroring = MirroringType::Horizontal; break;
				case 1: romData.Mirroring = MirroringType::Vertical; break;
				case 2: romData.Mirroring = MirroringType::ScreenAOnly; break;
				case 3: romData.Mirroring = MirroringType::ScreenBOnly; break;
				case 4: romData.Mirroring = MirroringType::FourScreens; break;
			}
		} else {
			//Unsupported/unused FourCCs: PCKn, CCKn, NAME, WRTR, READ, DINF, VROR
		}

		data = chunkEnd;

		return true;
	}

public:
	static int32_t GetMapperID(string mapperName)
	{
		string prefix = mapperName.substr(0, 4);
		if(prefix.compare("NES-") == 0 || prefix.compare("UNL-") == 0 || prefix.compare("HVC-") == 0 || prefix.compare("BTL-") == 0 || prefix.compare("BMC-") == 0) {
			mapperName = mapperName.substr(4);
		}

		auto result = _boardMappings.find(mapperName);
		if(result != _boardMappings.end()) {
			return result->second;
		}

		return UnifBoards::UnknownBoard;
	}

	RomData LoadRom(vector<uint8_t>& romFile)
	{
		RomData romData;

		//Skip header, version & null bytes, start reading at first chunk
		uint8_t* data = romFile.data() + 32;
		uint8_t* endOfFile = romFile.data() + romFile.size();

		while(ReadChunk(data, endOfFile, romData)) {
			//Read all chunks
		}

		for(int i = 0; i < 16; i++) {
			romData.PrgRom.insert(romData.PrgRom.end(), _prgChunks[i].begin(), _prgChunks[i].end());
			romData.ChrRom.insert(romData.ChrRom.end(), _chrChunks[i].begin(), _chrChunks[i].end());
		}

		if(romData.PrgRom.size() == 0 || _mapperName.empty()) {
			romData.Error = true;
		} else {
			vector<uint8_t> fullRom;
			fullRom.insert(fullRom.end(), romData.PrgRom.begin(), romData.PrgRom.end());
			fullRom.insert(fullRom.end(), romData.ChrRom.begin(), romData.ChrRom.end());

			romData.Format = RomFormat::Unif;
			romData.PrgCrc32 = CRC32::GetCRC(romData.PrgRom.data(), romData.PrgRom.size());
			romData.PrgChrCrc32 = CRC32::GetCRC(fullRom.data(), fullRom.size());
			romData.PrgChrMd5 = GetMd5Sum(fullRom.data(), fullRom.size());

			MessageManager::Log("PRG+CHR CRC32: 0x" + HexUtilities::ToHex(romData.PrgChrCrc32));
			MessageManager::Log("[UNIF] Board Name: " + _mapperName);
			MessageManager::Log("[UNIF] PRG ROM: " + std::to_string(romData.PrgRom.size() / 1024) + " KB");
			MessageManager::Log("[UNIF] CHR ROM: " + std::to_string(romData.ChrRom.size() / 1024) + " KB");
			if(romData.ChrRom.size() == 0) {
				MessageManager::Log("[UNIF] CHR RAM: 8 KB");
			}

			string mirroringType;
			switch(romData.Mirroring) {
				case MirroringType::Horizontal: mirroringType = "Horizontal"; break;
				case MirroringType::Vertical: mirroringType = "Vertical"; break;
				case MirroringType::ScreenAOnly: mirroringType = "1-Screen (A)"; break;
				case MirroringType::ScreenBOnly: mirroringType = "1-Screen (B)"; break;
				case MirroringType::FourScreens: mirroringType = "Four Screens"; break;
			}

			MessageManager::Log("[UNIF] Mirroring: " + mirroringType);
			MessageManager::Log("[UNIF] Battery: " + string(romData.HasBattery ? "Yes" : "No"));

			GameDatabase::SetGameInfo(romData.PrgChrCrc32, romData, !EmulationSettings::CheckFlag(EmulationFlags::DisableGameDatabase));

			if(romData.MapperID == UnifBoards::UnknownBoard) {
				MessageManager::DisplayMessage("Error", "UnsupportedMapper", "UNIF: " + _mapperName);
				romData.Error = true;
			}
		}

		return romData;
	}
};