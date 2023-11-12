#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>

#include <Editor/Config.h>
#include <Editor/Key.h>
#include <Editor/Row.h>

#define SCRIBE_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

Config config = Config();

void die(const char *s)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

void disableRawMode()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.getOrigTermios()) == -1)
		die("tcsetattr");
}

void enableRawMode()
{
  	if (tcgetattr(STDIN_FILENO, &config.getOrigTermios()) == -1) die("tcgetattr");
  	atexit(disableRawMode);

  	termios raw = config.getOrigTermios();
  	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  	raw.c_oflag &= ~(OPOST);
  	raw.c_cflag |= ~(CS8);
  	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  	raw.c_cc[VMIN] = 0;
  	raw.c_cc[VTIME] = 1;

  	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey()
{
	int nread;
	char c;

	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {

			if (seq[1] >= '0' && seq[1] <= '9') {

				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
					case '1': return HOME_KEY;
					case '3': return DEL_KEY;
					case '4': return END_KEY;
					case '5': return PAGE_UP;
					case '6': return PAGE_DOWN;
					case '7': return HOME_KEY;
					case '8': return END_KEY;
					}
				}

			} else {
				switch (seq[1]) {
				case 'A': return ARROW_UP;
				case 'B': return ARROW_DOWN;
				case 'C': return ARROW_RIGHT;
				case 'D': return ARROW_LEFT;
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
				}
			}
		} else if (seq[0] == '0') {
			switch (seq[1]) {
			case 'H': return HOME_KEY;
			case 'F': return END_KEY;
			}
		}

		return '\x1b';
	} else {
		return c;
	}
}

int getCursorPosition(int *rows, int *cols)
{
	char buf[32];
	uint i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols)
{
	winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

void editorOpen(char *filename)
{
	std::ifstream file(filename);

	if (!file.is_open())
        throw std::runtime_error("Failed to open file");

    std::string line;

    while (std::getline(file, line)) {

    	line.erase(std::find_if(line.rbegin(), line.rend(), [](int ch) {
        	return !std::isspace(ch);
    	}).base(), line.end());

    	config.addRow(line.length(), line);
    }

    file.close();
}

void editorDrawRows(std::string *ab)
{
	int y;
	for (y = 0; y < config.getScreenRows(); y++) {

		if (y >= config.getNumRows()) {

			if (config.getNumRows() == 0 && y == config.getScreenRows() / 3) {
				std::string greeting = "Scribe Editor -- version " + std::string(SCRIBE_VERSION);

				int padding = (config.getScreenCols() - greeting.length()) / 2;
				if (padding) {
					ab->append("~");
					padding--;
				}

				while (padding--) ab->append(" ");
				ab->append(greeting);

			} else {
				ab->append("~");
			}

		} else {
			ab->append(config.getRowAt(y).getChars());
		}

		ab->append("\x1b[K");
		if (y < config.getScreenRows() - 1) {
			ab->append("\r\n");
		}
	}
}

void editorRefreshScreen()
{
	std::string ab;

	ab.append("\x1b[?25l");
	ab.append("\x1b[H");

	editorDrawRows(&ab);

	ab.append("\x1b[" + std::to_string(config.getCoordinateY() + 1) + ";" + std::to_string(config.getCoordinateX() + 1) + "H");

	ab.append("\x1b[?25h");

	write(STDOUT_FILENO, ab.c_str(), ab.length());
}

void editorMoveCursor(int key)
{
	switch (key) {

	case ARROW_LEFT:
		if (config.getCoordinateX() != 0) {
			config.setCoordinateX(config.getCoordinateX() - 1);
		}
		break;

	case ARROW_RIGHT:
		if (config.getCoordinateX() != config.getScreenCols() - 1) {
			config.setCoordinateX(config.getCoordinateX() + 1);
		}
		break;

	case ARROW_UP:
		if (config.getCoordinateY() != 0) {
			config.setCoordinateY(config.getCoordinateY() - 1);
		}
		break;

	case ARROW_DOWN:
		if (config.getCoordinateY() != config.getScreenRows() - 1) {
			config.setCoordinateY(config.getCoordinateY() + 1);
		}
		break;
	}
}

void editorProcessKeypress()
{
	int c = editorReadKey();

	switch (c) {

	case CTRL_KEY('q'):
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		exit(0);
		break;

	case HOME_KEY:
		config.setCoordinateX(0);
		break;

	case END_KEY:
		config.setCoordinateX(config.getScreenCols() - 1);
		break;

	case PAGE_UP:
	case PAGE_DOWN: 
		{
			int times = config.getScreenRows();
			while (times--) {
				editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
		}
		break;

	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
		editorMoveCursor(c);
		break;

	}
}

void initEditor()
{
	if (getWindowSize(&config.getScreenRows(), &config.getScreenCols()) == -1) die("getWindowSize");
}

int main(int argc, char *argv[])
{
	enableRawMode();
	initEditor();

	if (argc >= 2) {
		editorOpen(argv[1]);
	}

  	while (1) {
  		editorRefreshScreen();
	  	editorProcessKeypress();
  	}

  	return 0;
}
