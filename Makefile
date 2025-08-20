CC = gcc
CFLAGS = -Wall -g -fsanitize=address
SRC = src
OBJ = obj

# list all .c files in src
SRCS = $(wildcard $(SRC)/*.c)
# replace .c with .o and put them in obj/
OBJS = $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(SRCS))

TARGET = shell

all: $(TARGET)

# link step
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# compile step (make sure obj/ exists)
$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ):
	mkdir -p $(OBJ)

clean:
	rm -f $(TARGET) $(OBJ)/*.o
