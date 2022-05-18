/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"


void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method); //head 메소드 지원
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method); //head 메소드 지원
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);


// 입력 ./tiny 5000
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr; //모든 형태의 소켓 주소를 저장하기에 충분

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  
  while (1) {
    clientlen = sizeof(clientaddr);
    /*accpet 함수 : 듣기식별자, 소켓주소구조체 주소, 주소(소켓구조체)의 길이*/
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    
    /* 소켓구조체 주소 > 호스트 이름: 호스트 주소, 서비스 이름 : 포트번호 스트링 표시로 변환*/
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    /* 트랜젝션 수행 */
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd){
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  /*
    Rio Package: 기존 유닉스 입출력함수의 불편한 점을 개선한 package
    편리하고 안정적이고 효울적인 IO제공
  */
  rio_t rio;//rio_readlineb를 위해 rio_t 타입 구조체의 읽기 버퍼 선언

  /* Read request line and headers*/
  Rio_readinitb(&rio,fd); //rio_t 구조체 타입을 읽기 버퍼와 연결한다.
  Rio_readlineb(&rio,buf,MAXLINE); // 다음 텍스트 줄을 파일 rio에서 읽고(종료문자 포함), 그것을 메모리 buf로 복사하고, 텍스트라인을 Null(0)로 종료시킴
  printf("Request headers:\n");
  printf("%s",buf);//buf에는 Request headers가 저장됨 (method, uri, version의 정보가 들어가 있음)
  sscanf(buf,"%s %s %s",method,uri,version);

  //strcasecmp 대소문을 무시하는 문자열 비교 함수. 같으면 0 리턴
  if(!(strcasecmp(method,"GET") == 0 || strcasecmp(method,"HEAD")==0)){ // GET과 HEAD 메소드 지원
    clienterror(fd,method,"501","Not impleneted","Tiny does not implement this method");
    return;
  }

  // request header 읽기
  read_requesthdrs(&rio);

  /*Parse URI from GET request*/
  //파일이 디스트 상에 있지않을때 
  is_static = parse_uri(uri,filename,cgiargs);
  if(stat(filename,&sbuf) < 0){
    clienterror(fd,filename,"404","Not fountd", "Tiny couldn't find this file");
    return;
  }

  if(is_static){ /* Serve static content */
    // 읽기 권한을 가지고있는지 검증
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){ 
      clienterror(fd,filename, "403","Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd,filename,sbuf.st_size,method);
  }
  else{ /*Serve dynamic content */
    // 실행 가능한 파일인지 검증
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      clienterror(fd,filename,"403","Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd,filename,cgiargs,method);
  }
}

/*
 * HTTP 응답을 응답 라인에 적절한 상태 코드와 상태 메시지와 함께 클라이언트에 보내며,
   브라우저 사용자에게 에러를 설명하는 응답 본체에 HTML 파일도 함께 보냄
   (HTML 응답은 본체에서 콘텐츠의 크기와 타입을 나타내야 한다)
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE],body[MAXBUF];

  /*Build the HHTP respone body*/
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n",body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body,longmsg,cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n",body);

  /*Print the HTTP response*/
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd,buf,strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];

  Rio_readlineb(rp,buf,MAXLINE);
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(rp,buf,MAXLINE);
    printf("%s",buf);
  }
  return;
}

// URI 예시 static: /mp4sample.mp4 , / , /adder.html 
//         dynamic: /cgi-bin/adder?first=1213&second=1232 
int parse_uri(char *uri, char *filename, char *cgiargs){
  char *ptr;

  if(!strstr(uri, "cgi-bin")){ /*Static content*/
    strcpy(cgiargs,"");       //CGI 인자 스트링 지우고  
    strcpy(filename,".");     //상대 리눅스 경로이름 변환
    strcat(filename,uri);     // strcat(*str1, *str2): str2를 str1에 연결하고 NULL 문자로 결과 스트링 종료
    //결과 cgiargs = "" 공백 문자열, filename = "./~~ or ./home.html

    // uri 문자열 끝이 / 일 경우 허전하지 말라고 home.html을 filename에 붙혀준다.
    if (uri[strlen(uri)-1] == '/'){
      strcat(filename, "home.html");
    }
    return 1;
  }
  else{ /*Dynamic content*/
    ptr = index(uri, '?');
    if(ptr){ //?가 존재하면
      strcpy(cgiargs,ptr+1); //주어진 인자값을 cgiargs에 저장
      *ptr = '\0'; //포인터는 문자열 마지막으로 위치
    }
    else strcpy(cgiargs, "");

    strcpy(filename,".");
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize, char *method){
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF], *fbuf;

  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n",buf);
  sprintf(buf, "%sConnection: close\r\n",buf);
  sprintf(buf, "%sContent-length: %d\r\n",buf,filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

  //writem = client 쪽에
  Rio_writen(fd, buf, strlen(buf));

  // 서버 쪽에 출력
  printf("Response headers: \n");
  printf("%s", buf);

  // 11.11문제 반영 - HEAD 메소드 지원
  if (strcasecmp(method, "HEAD") == 0) {
    return; 
  }

  /*Send response body to client*/
  srcfd = Open(filename,O_RDONLY,0);
  // srcp = Mmap(0,filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  // Close(srcfd);
  // Rio_writen(fd,srcp,filesize);
  // Munmap(srcp,filesize);
  fbuf = malloc(filesize);
  Rio_readn(srcfd,fbuf,filesize);
  Close(srcfd);
  Rio_writen(fd,fbuf,filesize);
  free(fbuf);
}

/**
 * get_file - Derive file type from filename
 */
void get_filetype(char *filename, char *filetype){
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filemane, char *cgiargs, char *method){
  char buf[MAXLINE], *emptylist[] = {NULL};

  /*Return first part of HTTP responese*/
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf,strlen(buf));

  //응답의 첫 번째 부분을 보낸 후 새로운 자식 프로세스 fork
  if (Fork() == 0){ /* child */
    /* Real server would ser all CGI vars here*/
    setenv("QUERY_STRING", cgiargs, 1);   // QUERY_STRING의 환경변수를 요청 URI의 CGI 인자들로 초기화
    setenv("REQUEST_METHOD", method, 1);  //  HEAD 메소드 지원 - REQUEST_METHOD: GET or POST

    Dup2(fd,STDOUT_FILENO);               // 자식은 자식의 표준 출력은 연결 파일 식별자로 재지정          
    Execve(filemane, emptylist, environ); // CGI프로그램 로드 후 실행
  }
  Wait(NULL); /*Parent waits for and reaps child*/
}