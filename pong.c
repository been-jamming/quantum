#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <complex.h>
#include <raylib.h>

#define resolution_x 80
#define resolution_y 50
#define pixel_size 14

int screen_resolution_x = resolution_x*pixel_size;
int screen_resolution_y = resolution_y*pixel_size;

double time_step = 0.0025;
int ticks_per_frame = 2;
int barrier_end = 20;

double state_real[resolution_x][resolution_y];
double state_imag[resolution_x][resolution_y];

uint8_t *pixels;

void initialize_state(void){
	complex entry;
	int x;
	int y;

	for(x = 0; x < resolution_x; x++){
		for(y = 0; y < resolution_y; y++){
			entry = cexp((x*4.21 + y*2.21)*2.5*2.0*M_PI/50.0*I)/exp(((x - resolution_x/2.0)/5.0)*((x - resolution_x/2.0)/5.0) + ((y - resolution_y/2.0)/5.0)*((y - resolution_y/2.0)/5.0));
			state_real[x][y] = creal(entry);
			state_imag[x][y] = cimag(entry);
		}
	}
}

Color get_color(complex value, double max_val){
	double norm, phase, red, green, blue;
	Color out;

	norm = cabs(value);
	phase = carg(value);
	phase = fmod(phase + 2*M_PI, 2*M_PI);

	if(phase <= M_PI/3 || phase >= 5*M_PI/3){
		red = 1.0;
		if(phase <= M_PI/3){
			green = phase*3/M_PI;
			blue = 0.0;
		} else {
			green = 0.0;
			blue = 1.0 - (phase - 5*M_PI/3)*3/M_PI;
		}
	} else if(phase >= M_PI/3 && phase <= M_PI){
		green = 1.0;
		if(phase <= 2*M_PI/3){
			red = (2*M_PI/3 - phase)*3/M_PI;
			blue = 0.0;
		} else {
			red = 0.0;
			blue = 1.0 - (M_PI - phase)*3/M_PI;
		}
	} else {
		blue = 1.0;
		if(phase <= 4*M_PI/3){
			red = 0.0;
			green = (4*M_PI/3 - phase)*3/M_PI;
		} else {
			red = 1.0 - (5*M_PI/3 - phase)*3/M_PI;
			green = 0.0;
		}
	}

	out = (Color) {.r = (int) (red*norm*norm/(max_val*max_val)*255.0),
			.g = (int) (green*norm*norm/(max_val*max_val)*255.0),
			.b = (int) (blue*norm*norm/(max_val*max_val)*255.0),
			.a = 255};
	return out;
}

void set_pixel_color(int x, int y, Color c){
	int index;

	index = (x + y*resolution_x)*4;
	pixels[index] = c.r;
	pixels[index + 1] = c.g;
	pixels[index + 2] = c.b;
	pixels[index + 3] = c.a;
}

void render_texture(Texture2D *texture){
	UpdateTexture(*texture, pixels);
	BeginDrawing();
	ClearBackground(BLACK);
	DrawTextureEx(*texture, (struct Vector2) {0, 0}, 0.0, pixel_size, WHITE);
	EndDrawing();
}

void render(Texture2D *texture){
	Color color;
	double norm, max_val = 0.0;
	int x, y;

	for(x = 0; x < resolution_x; x++){
		for(y = 0; y < resolution_y; y++){
			norm = cabs(state_real[x][y] + state_imag[x][y]*I);
			if(norm > max_val){
				max_val = norm;
			}
		}
	}

	for(x = 0; x < resolution_x; x++){
		for(y = 0; y < resolution_y; y++){
			color = get_color(state_real[x][y] + state_imag[x][y]*I, max_val);
			set_pixel_color(x, y, color);
		}
	}

	render_texture(texture);
}

int main(int argc, char **argv){
	Image canvas;
	Texture2D texture;
	int x, y, key, cont = 1;

	pixels = malloc(sizeof(uint8_t)*resolution_x*resolution_y*4);
	SetConfigFlags(FLAG_VSYNC_HINT);
	InitWindow(screen_resolution_x, screen_resolution_y, "Quantum Pong");

	if(!IsWindowReady()){
		fprintf(stderr, "Error: failed to open window.\n");
		return 1;
	}

	canvas = GenImageColor(resolution_x, resolution_y, BLACK);
	ImageFormat(&canvas, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	texture = LoadTextureFromImage(canvas);

	initialize_state();

	while(!WindowShouldClose()){
		render(&texture);
	}

	return 0;
}

