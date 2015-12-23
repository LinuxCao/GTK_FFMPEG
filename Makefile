CC= gcc
PROG_NAME= ffpaly
INCS= .
#利用wildcard函数产生一个所有以'.c' 结尾的文件的列表，然后存入SRCS中
SRCS= $(wildcard *.c)

#从xx.c 文件得到 xx.o 文件
OBJS=${SRCS:.c=.o}

#编译GTK和FFMPEG程序时要用到的库
LIBS=  libavcodec libavdevice libavfilter libavformat libavutil libswresample libswscale gtk+-2.0  


# -O2
CFLAGS=`pkg-config --cflags ${LIBS}` -g -Wall
LDFLAGS=`pkg-config --libs ${LIBS}`  -g -Wall
#
all: ${PROG_NAME}
${PROG_NAME}:${OBJS}
	${CC} -o ${PROG_NAME} ${OBJS} ${LDFLAGS}
#注意：上边”${CC}" 的前边有一个TAB键，而不是空格

#如果有头文件进行修改，则自动编译源文件
${OBJS}:${INCS}

.c.o:
	${CC} -c $<  ${CFLAGS}

clean:
	rm -f *.o  ${PROG_NAME}

rebuild: clean all
 
