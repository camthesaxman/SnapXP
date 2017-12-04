CC := gcc
RC := windres
CFLAGS := -Wall -Wextra -Wno-unused-parameter -std=c99 -O3
LIBS := -lcomctl32 -lgdi32 -mwindows
EXE_TARGET := SnapXP.exe
EXE_OBJECTS := main.o resources.o
DLL_TARGET := snaphook.dll
DLL_OBJECTS := snaphook.o

# Detect cmd.exe
ifneq ($(findstring cmd.exe, $(ComSpec)),)
  RM := del
endif

all: $(EXE_TARGET) $(DLL_TARGET)

clean:
	$(RM) $(EXE_TARGET) $(EXE_OBJECTS) $(DLL_TARGET) $(DLL_OBJECTS)

$(EXE_TARGET): $(EXE_OBJECTS)
	$(CC) $^ $(LIBS) -o $@

$(DLL_TARGET): $(DLL_OBJECTS)
	$(CC) $^ $(LIBS) -shared -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $^ -o $@
	
%.o: %.rc
	$(RC) $^ -o $@
