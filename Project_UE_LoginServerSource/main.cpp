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
#define IP_ADDRESS "127.0.0.1"
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
        char FailBuffer[PACKET_SIZE] = "-1";

        int RecvBytes = recv(CS, IdBuffer, sizeof(IdBuffer), 0);
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
                SendBytes = send(CS, &FailBuffer[TotalSentBytes], sizeof(FailBuffer) - TotalSentBytes, 0);
                TotalSentBytes += SendBytes;
            } while (TotalSentBytes < sizeof(FailBuffer));

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
                EnterCriticalSection(&ServerCS);
                int SendBytes = 0;
                int TotalSentBytes = 0;

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

                //send(CS, IdBuffer, sizeof(IdBuffer), 0);

                cout << "로그인이 성공하였습니다." << endl;

                sql::Statement* pstmt;
                pstmt = con->createStatement();
                pstmt->executeUpdate("UPDATE UserTable SET isLogin = true WHERE isLogin = false AND ID = '" + strID + "'");
                delete pstmt;
            }
            else
            {
                int SendBytes = 0;
                int TotalSentBytes = 0;

                char FailBuffer[PACKET_SIZE] = "-1";
                do
                {
                    SendBytes = send(CS, &FailBuffer[TotalSentBytes], sizeof(FailBuffer) - TotalSentBytes, 0);
                    TotalSentBytes += SendBytes;
                } while (TotalSentBytes < sizeof(FailBuffer));

                cout << "[로그인 실패]" << endl;
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