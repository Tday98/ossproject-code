COMP	= g++
COMPFLAGS  = -g3 -Wall -Wextra -Werror -std=c++14
TARGET1 = user
TARGET2 = oss 

OBJS1	= user.o
OBJS2	= oss.o

all:	$(TARGET1) $(TARGET2)

$(TARGET1):	$(OBJS1)
	$(COMP) -o $(TARGET1) $(OBJS1)

$(TARGET2):	$(OBJS2)
	$(COMP) -o $(TARGET2) $(OBJS2)

user.o:	user.cpp
	$(COMP) $(COMPFLAGS) -c user.cpp 

oss.o:	oss.cpp
	$(COMP) $(COMPFLAGS) -c oss.cpp

clean:
	/bin/rm -f *.o $(TARGET1) $(TARGET2)
