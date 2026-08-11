CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -Isrc
# -MMD -MP: sans suivi des en-tetes, modifier un .h ne recompilait rien et on
# se retrouvait a lier des objets construits sur deux versions d'une structure.
DEPFLAGS := -MMD -MP
LDLIBS := -lcurl -lpthread -lm -lcrypto

# Homebrew installs OpenSSL outside macOS' default compiler search paths.
ifeq ($(shell uname -s),Darwin)
LDLIBS += -lncurses
OPENSSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null || brew --prefix openssl 2>/dev/null)
ifeq ($(OPENSSL_PREFIX),)
$(error OpenSSL not found. Install it with: brew install openssl@3)
endif
CPPFLAGS += -I$(OPENSSL_PREFIX)/include
LDFLAGS += -L$(OPENSSL_PREFIX)/lib
else
LDLIBS += -lncursesw
endif

NAME := Offscript
OBJDIR := obj
SRCS_FILES := main.c json_min.c dialogue.c deepseek_client.c utils.c inputs.c npc.c display.c \
				map.c listes.c globals.c textutil.c story.c memory.c prompt.c \
				journal.c menu.c apikey.c options.c

SRCS := $(addprefix src/,$(SRCS_FILES))
OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRCS))

all: $(NAME)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	@echo "Compiling $<..."
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

$(NAME): $(OBJS)
	@echo "Linking $@..."
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	@echo "Cleaning up..."
	@rm -rf $(OBJDIR)

fclean: clean
	@echo "Removing executable..."
	@rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
