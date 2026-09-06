using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Collections.Generic;
public static class ProtocolProbe {
 static void Check(bool b,string message){if(!b)throw new Exception(message);}
 static byte[] Payload(ushort type, Action<BinaryWriter> fill){using(var m=new MemoryStream()){var w=new BinaryWriter(m);w.Write(type);fill(w);return m.ToArray();}}
 static byte[] ReadN(NetworkStream s,int n){byte[] b=new byte[n];int p=0;while(p<n){int r=s.Read(b,p,n-p);if(r==0)throw new Exception("Connection closed");p+=r;}return b;}
 static void Send(TcpClient c,byte[] payload){byte sum=0;foreach(byte b in payload)sum=unchecked((byte)(sum+b));byte[] frame=new byte[payload.Length+5];frame[0]=0x77;frame[1]=(byte)payload.Length;frame[2]=(byte)(payload.Length>>8);frame[3]=0x41;frame[4]=sum;Array.Copy(payload,0,frame,5,payload.Length);byte rk=frame[3],k=0x32,p=0,e=0;for(int i=4;i<frame.Length;i++){rk++;k++;p=unchecked((byte)(frame[i]^(p+rk)));e=unchecked((byte)(p^(e+k)));frame[i]=e;}c.GetStream().Write(frame,0,frame.Length);}
 static byte[] Recv(TcpClient c){var s=c.GetStream();byte[] h=ReadN(s,4);Check(h[0]==0x77,"Packet code");int n=h[1]+256*h[2];Check(n<=4096,"Payload limit");byte[] body=ReadN(s,n+1);byte rk=h[3],k=0x32,p=0,e=0;for(int i=0;i<body.Length;i++){rk++;k++;byte enc=body[i];byte np=unchecked((byte)(enc^(e+k)));body[i]=unchecked((byte)(np^(p+rk)));e=enc;p=np;}byte sum=0;for(int i=1;i<body.Length;i++)sum=unchecked((byte)(sum+body[i]));Check(sum==body[0],"Checksum");byte[] result=new byte[n];Array.Copy(body,1,result,0,n);return result;}
 static TcpClient Connect(int port){var c=new TcpClient();c.ReceiveTimeout=3000;c.SendTimeout=3000;c.Connect("127.0.0.1",port);return c;}
 static void FixedText(BinaryWriter w,string s){byte[] b=new byte[40];Encoding.Unicode.GetBytes(s).CopyTo(b,0);w.Write(b);}
 static byte[] Token(long id,char marker){string s=marker+id.ToString("D6");return Encoding.ASCII.GetBytes(s.PadRight(64,marker));}
 static void SendChatLogin(TcpClient c,long id,string user,string nick,byte[] token){Send(c,Payload(1,w=>{w.Write(id);FixedText(w,user);FixedText(w,nick);w.Write(token);}));}
 static void ExpectRejected(long id,string user,string nick,byte[] token,int chatPort){using(var c=Connect(chatPort)){SendChatLogin(c,id,user,nick,token);try{byte[] b=new byte[1];int n=c.GetStream().Read(b,0,1);if(n>0)throw new Exception("Invalid or replayed token received a response");}catch(IOException){ }catch(SocketException){ }}}
 public static string FullIntegration(int loginPort,int chatPort,int count){var clients=new List<TcpClient>();int loginOk=0,chatOk=0,moves=0,messages=0;try{
  for(int i=0;i<count;i++){long id=100000+i;byte[] token=Token(id,'T');string user="probe"+id,nick="local"+id;
   using(var login=Connect(loginPort)){Send(login,Payload(101,w=>{w.Write(id);w.Write(token);}));byte[] r=Recv(login);Check(r.Length==159&&BitConverter.ToUInt16(r,0)==102&&BitConverter.ToInt64(r,2)==id&&r[10]==1,"Login server response");Check(Encoding.Unicode.GetString(r,11,40).TrimEnd('\0')==user,"Login DB user");Check(Encoding.Unicode.GetString(r,51,40).TrimEnd('\0')==nick,"Login DB nickname");Check(BitConverter.ToUInt16(r,157)==chatPort,"Returned chat endpoint");loginOk++;}
   if(i==0)ExpectRejected(id,user,nick,Token(id,'X'),chatPort);
   var chat=Connect(chatPort);clients.Add(chat);SendChatLogin(chat,id,user,nick,token);byte[] lr=Recv(chat);Check(BitConverter.ToUInt16(lr,0)==2&&lr[2]==1&&BitConverter.ToInt64(lr,3)==id,"Chat token login");chatOk++;
   Send(chat,Payload(3,w=>{w.Write(id);w.Write((ushort)10);w.Write((ushort)10);}));byte[] mr=Recv(chat);Check(BitConverter.ToUInt16(mr,0)==4&&BitConverter.ToInt64(mr,2)==id,"Sector move");moves++;
  }
  byte[] message=Encoding.Unicode.GetBytes("redis-auth-chat-proof");for(int sender=0;sender<count;sender++){long id=100000+sender;Send(clients[sender],Payload(5,w=>{w.Write(id);w.Write((ushort)message.Length);w.Write(message);}));foreach(var c in clients){byte[] r=Recv(c);Check(BitConverter.ToUInt16(r,0)==6&&BitConverter.ToInt64(r,2)==id,"Broadcast account");Check(BitConverter.ToUInt16(r,90)==message.Length,"Broadcast length");for(int j=0;j<message.Length;j++)Check(r[92+j]==message[j],"Broadcast content");messages++;}}
  ExpectRejected(100000,"probe100000","local100000",Token(100000,'T'),chatPort);
  bool originalEvicted=false;try{Send(clients[0],Payload(3,w=>{w.Write((long)100000);w.Write((ushort)11);w.Write((ushort)11);}));Recv(clients[0]);}catch{originalEvicted=true;}Check(!originalEvicted,"Replay attempt evicted the authenticated session");
  return "PASS login="+loginOk+" redisTokenChatLogin="+chatOk+" invalidTokenRejected=1 replayRejected=1 sectorMoves="+moves+" validatedChatResponses="+messages+" originalSessionEvictedByReplayAttempt="+originalEvicted;
 }finally{foreach(var c in clients)c.Close();}}
 public static string LoginDatabase(int port){using(var c=Connect(port)){Send(c,Payload(101,w=>{w.Write((long)100000);w.Write(new byte[64]);}));byte[] r=Recv(c);Check(r.Length==159,"Login response length");Check(BitConverter.ToUInt16(r,0)==102&&BitConverter.ToInt64(r,2)==100000&&r[10]==1,"DB login response");Check(Encoding.Unicode.GetString(r,11,40).TrimEnd('\0')=="probe100000","DB user id");Check(Encoding.Unicode.GetString(r,51,40).TrimEnd('\0')=="local100000","DB nickname");}using(var c=Connect(port)){Send(c,Payload(101,w=>{w.Write((long)999999);w.Write(new byte[64]);}));byte[] r=Recv(c);Check(BitConverter.ToUInt16(r,0)==102&&r[10]==0,"Missing account must fail");}return "PASS MySQL existing account accepted, DB fields matched, missing account rejected; caller-supplied zero session key accepted (no token issuance).";}
 public static string Run(string kind,int port,int count){var clients=new List<TcpClient>();int logins=0,moves=0,responses=0;try{
  for(int i=0;i<count;i++){var c=Connect(port);clients.Add(c);long id=100000+i;Send(c,Payload((ushort)(kind=="GroupEcho"?1001:1),w=>{w.Write(id);if(kind!="GroupEcho"){FixedText(w,"probe"+id);FixedText(w,"local"+id);}w.Write(new byte[64]);}));byte[] r=Recv(c);Check(BitConverter.ToUInt16(r,0)==(kind=="GroupEcho"?1002:2)&&r[2]==1&&BitConverter.ToInt64(r,3)==id,"Login response");logins++;
   if(kind!="GroupEcho"){Send(c,Payload(3,w=>{w.Write(id);w.Write((ushort)10);w.Write((ushort)10);}));r=Recv(c);Check(BitConverter.ToUInt16(r,0)==4&&BitConverter.ToInt64(r,2)==id,"Sector response");moves++;}
  }
  if(kind=="GroupEcho") {for(int round=0;round<10;round++){for(int i=0;i<count;i++){long id=100000+i;long tick=round*1000+i;Send(clients[i],Payload(5000,w=>{w.Write(id);w.Write(tick);}));}for(int i=0;i<count;i++){byte[] r=Recv(clients[i]);Check(r.Length==18&&BitConverter.ToUInt16(r,0)==5001&&BitConverter.ToInt64(r,2)==100000+i&&BitConverter.ToInt64(r,10)==round*1000+i,"Echo content mismatch");responses++;}}}
  else {byte[] message=Encoding.Unicode.GetBytes("local-chat-proof");for(int sender=0;sender<count;sender++){long id=100000+sender;Send(clients[sender],Payload(5,w=>{w.Write(id);w.Write((ushort)message.Length);w.Write(message);}));foreach(var c in clients){byte[] r=Recv(c);Check(BitConverter.ToUInt16(r,0)==6&&BitConverter.ToInt64(r,2)==id,"Chat sender mismatch");Check(r.Length==92+message.Length&&BitConverter.ToUInt16(r,90)==message.Length,"Chat length mismatch");for(int j=0;j<message.Length;j++)Check(r[92+j]==message[j],"Chat content mismatch");responses++;}}}
  return "PASS kind="+kind+" simultaneousClients="+count+" logins="+logins+" sectorMoves="+moves+" validatedResponses="+responses;
 }finally{foreach(var c in clients)c.Close();}}
}
