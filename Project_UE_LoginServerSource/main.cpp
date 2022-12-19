#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <stdlib.h>

#include <WinSock2.h>
#include <process.h>
#include <vector>
#include <string>

#include "jdbc/mysql_connection.h"
#include "jdbc/cppconn/driver.h"
#include "jdbc/cppconn/exception.h"
#include "jdbc/cppconn/prepared_statement.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment (lib, "mysqlcppconn.lib")

#define PORT 19936
#define IP_ADDRESS "172.16.2.84"
#define PACKET_SIZE 200

using namespace std;

const string server = "tcp://127.0.0.1:3306";
const string username = "root";
const string password = "1234";

vector<SOCKET> vSocketList;

CRITICAL_SECTION ServerCS;

sql::Driver* driver = nullptr;
sql::Connection* con = nullptr;
sql::Statement* stmt = nullptr;
sql::PreparedStatement* pstmt = nullptr;
sql::ResultSet* rs = nullptr;

std::string MultiByteToUtf8(std::string multibyte_str)
{
	char* pszIn = new char[multibyte_str.length() + 1];
	strncpy_s(pszIn, multibyte_str.length() + 1, multibyte_str.c_str(), multibyte_str.length());

	std::string resultString;

	int nLenOfUni = 0, nLenOfUTF = 0;
	wchar_t* uni_wchar = NULL;
	char* pszOut = NULL;

	// 1. ANSI(multibyte) Length
	if ((nLenOfUni = MultiByteToWideChar(CP_ACP, 0, pszIn, (int)strlen(pszIn), NULL, 0)) <= 0)
		return 0;

	uni_wchar = new wchar_t[nLenOfUni + 1];
	memset(uni_wchar, 0x00, sizeof(wchar_t) * (nLenOfUni + 1));

	// 2. ANSI(multibyte) ---> unicode
	nLenOfUni = MultiByteToWideChar(CP_ACP, 0, pszIn, (int)strlen(pszIn), uni_wchar, nLenOfUni);

	// 3. utf8 Length
	if ((nLenOfUTF = WideCharToMultiByte(CP_UTF8, 0, uni_wchar, nLenOfUni, NULL, 0, NULL, NULL)) <= 0)
	{
		delete[] uni_wchar;
		return 0;
	}

	pszOut = new char[nLenOfUTF + 1];
	memset(pszOut, 0, sizeof(char) * (nLenOfUTF + 1));

	// 4. unicode ---> utf8
	nLenOfUTF = WideCharToMultiByte(CP_UTF8, 0, uni_wchar, nLenOfUni, pszOut, nLenOfUTF, NULL, NULL);
	pszOut[nLenOfUTF] = 0;
	resultString = pszOut;

	delete[] uni_wchar;
	delete[] pszOut;

	return resultString;
}

unsigned WINAPI WorkThread(void* Args)
{
	SOCKET CS = *(SOCKET*)Args;

	while (true)
	{
		char IdBuffer[PACKET_SIZE] = { 0, };
		char PwdBuffer[PACKET_SIZE] = { 0, };
		char LoginBuffer[PACKET_SIZE] = "true";
		char NotExitsBuffer[PACKET_SIZE] = "Not_Exits";
		char AcceptingBuffer[PACKET_SIZE] = "Already_Accpet";

		char Buffer[PACKET_SIZE] = { 0, };
		int RecvBytes = recv(CS, Buffer, sizeof(Buffer), 0);

		string strPacket = Buffer;

		if (strPacket == "LogoutPacket")
		{
			char PlayerNameBuffer[PACKET_SIZE] = { 0, };
			RecvBytes = recv(CS, PlayerNameBuffer, sizeof(PlayerNameBuffer), 0);

			string PlayerName = PlayerNameBuffer;

			sql::Statement* pstmt;
			pstmt = con->createStatement();
			pstmt->executeUpdate("UPDATE UserTable SET isLogin = false WHERE PlayerName = '" + PlayerName + "'");
			delete pstmt;

			closesocket(CS);
			EnterCriticalSection(&ServerCS);
			vSocketList.erase(find(vSocketList.begin(), vSocketList.end(), CS));
			LeaveCriticalSection(&ServerCS);
			break;
		}
		else
		{
			RecvBytes = recv(CS, IdBuffer, sizeof(IdBuffer), 0);
			if (RecvBytes <= 0)
			{
				cout << "클라이언트 연결 종료 : " << CS << '\n';

				closesocket(CS);
				EnterCriticalSection(&ServerCS);
				vSocketList.erase(find(vSocketList.begin(), vSocketList.end(), CS));
				LeaveCriticalSection(&ServerCS);
				break;
			}
			IdBuffer[PACKET_SIZE - 1] = '\0';
			string strID = IdBuffer;

			RecvBytes = recv(CS, PwdBuffer, sizeof(PwdBuffer), 0);
			if (RecvBytes <= 0)
			{
				cout << "클라이언트 연결 종료 : " << CS << '\n';

				closesocket(CS);
				EnterCriticalSection(&ServerCS);
				vSocketList.erase(find(vSocketList.begin(), vSocketList.end(), CS));
				LeaveCriticalSection(&ServerCS);
				break;
			}
			PwdBuffer[PACKET_SIZE - 1] = '\0';
			string strPWD = PwdBuffer;

			string strLogin = LoginBuffer;

			pstmt = con->prepareStatement("SELECT isLogin FROM UserTable WHERE `ID` = ?");
			pstmt->setString(1, strID);
			rs = pstmt->executeQuery();

			bool isLogin = false;

			while (rs->next())
			{
				isLogin = rs->getBoolean("isLogin");
				break;
			}

			if (isLogin == true)
			{
				EnterCriticalSection(&ServerCS);
				int SendBytes = 0;
				int TotalSentBytes = 0;

				do
				{
					SendBytes = send(CS, &AcceptingBuffer[TotalSentBytes], sizeof(AcceptingBuffer) - TotalSentBytes, 0);
					TotalSentBytes += SendBytes;

					cout << "접속중인 아이디" << '\n';

				} while (TotalSentBytes < sizeof(AcceptingBuffer));

				if (SendBytes <= 0)
				{
					closesocket(CS);
					EnterCriticalSection(&ServerCS);
					vSocketList.erase(find(vSocketList.begin(), vSocketList.end(), CS));
					LeaveCriticalSection(&ServerCS);
					break;
				}
				LeaveCriticalSection(&ServerCS);

				cout << "이미 접속중인 클라이언트입니다." << '\n';
			}
			else
			{
				pstmt = con->prepareStatement("SELECT COUNT(1) AS `CNT` FROM UserTable WHERE `ID`= ? AND `PWD`= (?)");
				pstmt->setString(1, strID);
				pstmt->setString(2, strPWD);
				rs = pstmt->executeQuery();

				bool isExists = false;
				while (rs->next())
				{
					isExists = rs->getInt("CNT") > 0 ? true : false;
					break;
				}
				if (isExists)
				{
					pstmt = con->prepareStatement("SELECT PlayerName FROM UserTable WHERE `ID` = ?");
					pstmt->setString(1, strID);
					rs = pstmt->executeQuery();

					char Name[PACKET_SIZE] = { 0, };
					string PName;

					while (rs->next())
					{
						PName = rs->getString("PlayerName");
						break;
					}

					memcpy(Name, PName.c_str(), PName.size());

					EnterCriticalSection(&ServerCS);
					int SendBytes = 0;
					int TotalSentBytes = 0;

					do
					{
						SendBytes = send(CS, &Name[TotalSentBytes], sizeof(Name) - TotalSentBytes, 0);
						TotalSentBytes += SendBytes;
					} while (TotalSentBytes < sizeof(Name));

					if (SendBytes <= 0)
					{
						closesocket(CS);
						EnterCriticalSection(&ServerCS);
						vSocketList.erase(find(vSocketList.begin(), vSocketList.end(), CS));
						LeaveCriticalSection(&ServerCS);
						break;
					}
					LeaveCriticalSection(&ServerCS);

					Sleep(1000);

					EnterCriticalSection(&ServerCS);
					SendBytes = 0;
					TotalSentBytes = 0;

					char Success[PACKET_SIZE] = "LoginSuccess";
					do
					{
						SendBytes = send(CS, &Success[TotalSentBytes], sizeof(Success) - TotalSentBytes, 0);
						TotalSentBytes += SendBytes;
					} while (TotalSentBytes < sizeof(Success));

					if (SendBytes <= 0)
					{
						closesocket(CS);
						EnterCriticalSection(&ServerCS);
						vSocketList.erase(find(vSocketList.begin(), vSocketList.end(), CS));
						LeaveCriticalSection(&ServerCS);
						break;
					}
					LeaveCriticalSection(&ServerCS);

					cout << "로그인이 성공하였습니다." << endl;

					sql::Statement* pstmt;
					pstmt = con->createStatement();
					pstmt->executeUpdate("UPDATE UserTable SET isLogin = true WHERE isLogin = false AND ID = '" + strID + "'");
					delete pstmt;

					cout << "데이터베이스 업데이트 성공 !" << '\n';
				}
				else
				{
					int SendBytes = 0;
					int TotalSentBytes = 0;
					do
					{
						SendBytes = send(CS, &NotExitsBuffer[TotalSentBytes], sizeof(NotExitsBuffer) - TotalSentBytes, 0);
						TotalSentBytes += SendBytes;
					} while (TotalSentBytes < sizeof(NotExitsBuffer));

					cout << "존재하지 않는 아이디입니다." << endl;
				}
			}
		}
	}
	return 0;
}

int main()
{
	driver = get_driver_instance();
	con = driver->connect(server, username, password);
	con->setSchema("UE4SERVER");

	cout << "[로그인 서버 활성화]" << '\n';

	InitializeCriticalSection(&ServerCS);

	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	SOCKET SS = socket(AF_INET, SOCK_STREAM, 0);

	SOCKADDR_IN SA = { 0, };
	SA.sin_family = AF_INET;
	SA.sin_addr.S_un.S_addr = inet_addr(IP_ADDRESS);
	SA.sin_port = htons(PORT);

	if (::bind(SS, (SOCKADDR*)&SA, sizeof(SA)) != 0) { exit(-3); };
	if (listen(SS, SOMAXCONN) == SOCKET_ERROR) { exit(-4); };

	cout << "클라이언트 연결을 기다리는 중입니다......." << '\n';

	while (true)
	{
		SOCKADDR_IN CA = { 0, };
		int sizeCA = sizeof(CA);
		SOCKET CS = accept(SS, (SOCKADDR*)&CA, &sizeCA);

		cout << "클라이언트 접속 : " << CS << '\n';

		EnterCriticalSection(&ServerCS);
		vSocketList.push_back(CS);
		LeaveCriticalSection(&ServerCS);

		HANDLE hThread = (HANDLE)_beginthreadex(0, 0, WorkThread, (void*)&CS, 0, 0);
	}
	closesocket(SS);

	WSACleanup();
}