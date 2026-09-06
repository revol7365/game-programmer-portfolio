#pragma once

struct Sector {
	SRWLOCK lock;
	std::vector<Player*> players;

	Sector() {
		InitializeSRWLock(&lock);
		players.reserve(1024);
	}
};

// 섹터 하나의 좌표 정보 
struct SectorPos
{
	int iX;
	int iY;

	bool operator==(const SectorPos& other) const
	{
		return iX == other.iX && iY == other.iY;
	}

	bool operator!=(const SectorPos& other) const
	{
		return !(*this == other);
	}
};


// 특정 위치 주변의 9개 섹터 정보 
struct SectorAround
{
	int iCount;
	SectorPos Around[9];
};
