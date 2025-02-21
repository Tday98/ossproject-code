COMP	= g++
COMPFLAGS  = -g3 -Wall -Wextra -Werror -std=c++14
TARGET1 = worker
TARGET2 = oss 

OBJS1	= worker.o
OBJS2	= oss.o

all:	$(TARGET1) $(TARGET2)

$(TARGET1):	$(OBJS1)
	$(COMP) -o $(TARGET1) $(OBJS1)

$(TARGET2):	$(OBJS2)
	$(COMP) -o $(TARGET2) $(OBJS2)

worker.o:	worker.cpp
	$(COMP) $(COMPFLAGS) -c worker.cpp 

oss.o:	oss.cpp
	$(COMP) $(COMPFLAGS) -c oss.cpp

clean:
	/bin/rm -f *.o $(TARGET1) $(TARGET2)
