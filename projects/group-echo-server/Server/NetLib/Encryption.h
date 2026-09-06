#pragma once

#include <string>
#include "Define.h"
namespace PacketEncryption {
	// 체크섬 계산
	inline unsigned char CalculateCheckSum(const char* pPayload, int iSize) {
		unsigned char checkSum = 0;
		for (int i = 0; i < iSize; ++i) {
			checkSum += (unsigned char)pPayload[i];
		}
		return checkSum;
	}

	// 암호화 (In-place)
	inline void Encode(const unsigned char dynamicKey, char* pData, int iSize) {
		unsigned char rk = dynamicKey;
		unsigned char k = PACKET_KEY; // Define.h에 정의된 고정값
		unsigned char currentP = 0;
		unsigned char currentE = 0;

		for (int i = 0; i < iSize; i++) {
			rk++;
			k++;

			unsigned char raw = (unsigned char)pData[i];

			// D2 ^ (P1 + RK + 2) = P2
			currentP = raw ^ (currentP + rk);
			// P2 ^ (E1 + K + 2) = E2
			currentE = currentP ^ (currentE + k);

			pData[i] = (char)currentE;
		}
	}

	// 복호화 (In-place)
	inline void Decode(const unsigned char dynamicKey, char* pData, int iSize) {
		unsigned char rk = dynamicKey;
		unsigned char k = PACKET_KEY;
		unsigned char currentP = 0;
		unsigned char currentE = 0;

		for (int i = 0; i < iSize; i++) {
			rk++;
			k++;

			unsigned char encrypted = (unsigned char)pData[i];

			// P1 = E1^(K + 1)
			// P2 = E2^(E1 + K + 2)
			unsigned char newP = encrypted ^ (currentE + k);
			// D1 = P1^(RK + 1)
			// D2 = P2^(P1 + RK + 2)		
			unsigned char raw = newP ^ (currentP + rk);

			// 다음 루프를 위한 상태 갱신
			currentE = encrypted;
			currentP = newP;

			pData[i] = (char)raw;
		}
	}
}
