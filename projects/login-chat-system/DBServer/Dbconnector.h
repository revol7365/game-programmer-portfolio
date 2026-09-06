#pragma once

// C++ 컴파일러가 C 함수 이름을 변조하지 못하게 보호 (LNK2001 해결)
extern "C" {
#include <mysql.h>
}

#include <cstdio>
#include <cstring>



class DBConnector
{
public:
   DBConnector() : pConn_(nullptr) {}

   ~DBConnector()
   {
       Disconnect();
   }

   MYSQL* GetConnection() { return pConn_; }

   //--------------------------------------------------------------------------------------------
   // DB 접속
   //--------------------------------------------------------------------------------------------
   bool Connect(const char* host, const char* user, const char* password,
       const char* db, unsigned int port = 3306)
   {
       pConn_ = mysql_init(nullptr);
       if (pConn_ == nullptr)
       {
           // printf("[DBConnector] mysql_init 실패\n");
           return false;
       }

       // 연결
       if (mysql_real_connect(pConn_, host, user, password, db, port, nullptr, 0) == nullptr)
       {
           // printf("[DBConnector] 연결 실패: %s\n", mysql_error(pConn_));
           mysql_close(pConn_);
           pConn_ = nullptr;
           return false;
       }

       // UTF-8 설정
       mysql_set_character_set(pConn_, "utf8mb4");

       // printf("[DBConnector] DB 연결 성공 (%s:%d, DB:%s)\n", host, port, db);
       return true;
   }

   //--------------------------------------------------------------------------------------------
   // 연결 해제
   //--------------------------------------------------------------------------------------------
   void Disconnect()
   {
       if (pConn_ != nullptr)
       {
           mysql_close(pConn_);
           pConn_ = nullptr;
           // printf("[DBConnector] DB 연결 해제\n");
       }
   }

   //--------------------------------------------------------------------------------------------
   // 쿼리 실행 (INSERT, UPDATE, DELETE 등)
   //--------------------------------------------------------------------------------------------
   bool Execute(const char* query)
   {
       if (pConn_ == nullptr)
       {
           // printf("[DBConnector] 연결 안됨\n");
           return false;
       }

       if (mysql_query(pConn_, query) != 0)
       {
           // printf("[DBConnector] 쿼리 실패: %s\n  쿼리: %s\n", mysql_error(pConn_), query);
           return false;
       }

       return true;
   }

   //--------------------------------------------------------------------------------------------
   // 트랜잭션
   //--------------------------------------------------------------------------------------------
   bool BeginTransaction()
   {
       return Execute("START TRANSACTION");
   }

   bool Commit()
   {
       return Execute("COMMIT");
   }

   bool Rollback()
   {
       return Execute("ROLLBACK");
   }

   //--------------------------------------------------------------------------------------------
   // 에러 메시지
   //--------------------------------------------------------------------------------------------
   const char* GetLastError() const
   {
       if (pConn_ == nullptr)
           return "Not connected";
       return mysql_error(pConn_);
   }

private:
   MYSQL* pConn_;
};