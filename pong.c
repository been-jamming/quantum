#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <complex.h>
#include <raylib.h>

#ifndef M_PI
	#define M_PI (3.1415926535898)
#endif

#define resolution_x 121
#define resolution_y 62
#define pixel_size 14
#define paddle_size 15
#define barrier_end 20
#define paddle_speed 1.0
#define target_fps 60
#define font_size 100
#define max_speed 1.4
#define max_round_time 60.0
#define localization 25.0
#define background_color ((Color) {.r = 128, .g = 128, .b = 128, .a = 255})

int screen_resolution_x = resolution_x*pixel_size;
int screen_resolution_y = resolution_y*pixel_size;
int image_start_x;
int image_start_y;
int image_width;
int image_height;
double paddle0_pos = 0;
double paddle1_pos = 0;

double time_step = 4.0;
int ticks_per_frame = 4;

double state_real[resolution_x][resolution_y];
double state_imag[resolution_x][resolution_y];
double next_state_real[resolution_x][resolution_y];
double next_state_imag[resolution_x][resolution_y];

uint8_t *pixels;

double p0_previous_score = 0.0;
double p1_previous_score = 0.0;
double p0_round_score = 0.0;
double p1_round_score = 0.0;

double round_start_time = 0.0;
double critical_mass_time = -1.0;
double current_time;

void initialize_state(double x_dir, double y_dir, double localize_x, double localize_y){
	complex entry, entry_x, entry_y;
	int x;
	int y;

	for(x = 0; x < resolution_x; x++){
		for(y = 0; y < resolution_y; y++){
			entry_x = cexp(-(x - resolution_x/2.0)*(x - resolution_x/2.0)/(localize_x) + x*x_dir*2.0*M_PI*I);
			entry_y = cexp(-(y - resolution_y/2.0)*(y - resolution_y/2.0)/(localize_y) + y*y_dir*2.0*M_PI*I);
			entry = entry_x*entry_y;
			state_real[x][y] = creal(entry);
			state_imag[x][y] = cimag(entry);
		}
	}
}

void normalize(double (*next_state_real)[resolution_y], double (*next_state_imag)[resolution_y], double (*state_imag)[resolution_y]){
	int x, y;
	double total = 0.0, norm;

	for(x = 0; x < resolution_x; x++){
		for(y = 0; y < resolution_y; y++){
			total += next_state_real[x][y]*next_state_real[x][y] + next_state_imag[x][y]*state_imag[x][y];
		}
	}

	norm = sqrt(total);

	for(x = 0; x < resolution_x; x++){
		for(y = 0; y < resolution_y; y++){
			next_state_real[x][y] /= norm;
			next_state_imag[x][y] /= norm;
		}
	}
}

void start_new_round(void){
	int r;
	double speed, angle, x_dir, y_dir, localize_x, localize_y;

	p0_previous_score += p0_round_score;
	p1_previous_score += p1_round_score;
	p0_round_score = 0.0;
	p1_round_score = 0.0;

	r = GetRandomValue(500, 1000);
	speed = r*max_speed/1000;

	r = GetRandomValue(0, 628);
	angle = r*2.0*M_PI/628;
	x_dir = cos(angle);
	y_dir = sin(angle);

	r = GetRandomValue(50, 200);
	localize_x = localization*r/100.0;
	r = GetRandomValue(50, 200);
	localize_y = localization*r/100.0;

	initialize_state(x_dir*speed, y_dir*speed, localize_x, localize_y);
	normalize(state_real, state_imag, state_imag);

	round_start_time = current_time;
	critical_mass_time = -1.0;
}

int behind_paddles(int x, int y){
	return x < barrier_end || x >= resolution_x - barrier_end;
}

int in_paddle(int x, int y){
	return (x == barrier_end && y >= paddle0_pos && y < paddle0_pos + paddle_size) ||
	       (x == resolution_x - barrier_end - 1 && y >= paddle1_pos && y < paddle1_pos + paddle_size);
}

int in_center(int x, int y){
	return (resolution_x%2 == 1 && x == resolution_x/2 && y%10 < 5) ||
	       (resolution_x%2 == 0 && (x == resolution_x/2 || x == resolution_x/2 + 1) && y%10 < 5);
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

void render_texture(Texture2D *texture, int x_pos, int y_pos, double scale){
	UpdateTexture(*texture, pixels);
	DrawTextureEx(*texture, (struct Vector2) {x_pos, y_pos}, 0.0, scale, WHITE);
}

void render(Texture2D *texture){
	Color color;
	Vector2 text_size;
	double norm, max_val = 0.0, scale, screen_aspect, target_aspect;
	int x, y, text_pos_x_p0, text_pos_y_p0, text_pos_x_p1, text_pos_y_p1;
	char score_str_p0[8];
	char score_str_p1[8];

	screen_resolution_x = GetScreenWidth();
	screen_resolution_y = GetScreenHeight();
	screen_aspect = ((double) screen_resolution_x)/((double) screen_resolution_y);
	target_aspect = ((double) resolution_x)/((double) resolution_y);
	if(screen_aspect > target_aspect){
		image_start_x = (screen_resolution_x - target_aspect*screen_resolution_y)/2.0;
		image_start_y = 0.0;
		image_width = target_aspect*screen_resolution_y;
		image_height = screen_resolution_y;
		scale = ((double) screen_resolution_y)/resolution_y;
	} else {
		image_start_x = 0.0;
		image_start_y = (screen_resolution_y - ((double) screen_resolution_x)/target_aspect)/2.0;
		image_width = screen_resolution_x;
		image_height = ((double) screen_resolution_x)/target_aspect;
		scale = ((double) screen_resolution_x)/resolution_x;
	}

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
			if(in_paddle(x, y)){
				color = WHITE;
			} else {
				color = get_color(state_real[x][y] + state_imag[x][y]*I, max_val);
				if(behind_paddles(x, y)){
					color.r = (color.r + 128)/2;
				}
				if(in_center(x, y)){
					color.r = (color.r + 128)/2;
					color.g = (color.g + 128)/2;
					color.b = (color.b + 128)/2;
				}
			}
			set_pixel_color(x, y, color);
		}
	}

	BeginDrawing();
	ClearBackground(background_color);
	render_texture(texture, image_start_x, image_start_y, scale);

	snprintf(score_str_p0, 8, "%.2lf", p0_previous_score + p0_round_score);
	score_str_p0[7] = '\0';
	snprintf(score_str_p1, 8, "%.2lf", p1_previous_score + p1_round_score);
	score_str_p1[7] = '\0';

	text_size = MeasureTextEx(GetFontDefault(), score_str_p0, font_size, font_size/10);
	text_pos_x_p0 = image_width*(((float) barrier_end)/((float) resolution_x)*0.5 + 0.25) - text_size.x/2.0 + image_start_x;
	text_pos_y_p0 = image_height*0.25 - text_size.y/2.0 + image_start_y;

	text_size = MeasureTextEx(GetFontDefault(), score_str_p1, font_size, font_size/10);
	text_pos_x_p1 = image_width*((1 - (barrier_end + 1.0)/((float) resolution_x))*0.5 + 0.25) - text_size.x/2.0 + image_start_x;
	text_pos_y_p1 = image_height*0.25 - text_size.y/2.0 + image_start_y;

	DrawTextEx(GetFontDefault(), score_str_p0, (Vector2) {.x = text_pos_x_p0, .y = text_pos_y_p0}, font_size, font_size/10, (Color) {.r = 255, .g = 255, .b = 255, .a = 128});
	DrawTextEx(GetFontDefault(), score_str_p1, (Vector2) {.x = text_pos_x_p1, .y = text_pos_y_p1}, font_size, font_size/10, (Color) {.r = 255, .g = 255, .b = 255, .a = 128});
	EndDrawing();
}

double get_barrier_momentum_p0(int y, double (*state_real)[resolution_y], double (*state_imag)[resolution_y]){
	complex z0, z1, z2;

	z0 = state_real[barrier_end - 1][y] + state_imag[barrier_end - 1][y]*I;
	z1 = state_real[barrier_end][y] + state_imag[barrier_end][y]*I;
	z2 = state_real[barrier_end + 1][y] + state_imag[barrier_end + 1][y]*I;

	return creal(-I*conj(z2 - z0)*z1);
}

double get_barrier_momentum_p1(int y, double (*state_real)[resolution_y], double (*state_imag)[resolution_y]){
	complex z0, z1, z2;

	z2 = state_real[resolution_x - barrier_end][y] + state_imag[resolution_x - barrier_end][y]*I;
	z1 = state_real[resolution_x - barrier_end - 1][y] + state_imag[resolution_x - barrier_end - 1][y]*I;
	z0 = state_real[resolution_x - barrier_end - 2][y] + state_imag[resolution_x - barrier_end - 2][y]*I;

	return creal(-I*conj(z2 - z0)*z1);
}

void get_second_derivative(double *out_x, double *out_y, double (*vector)[resolution_y], int x, int y, double (*state_real)[resolution_y], double (*state_imag)[resolution_y]){
	double p0_momentum, p1_momentum;
	double x0, x1, x2, y0, y1, y2;

	p0_momentum = get_barrier_momentum_p0(y, state_real, state_imag);
	p1_momentum = get_barrier_momentum_p1(y, state_real, state_imag);

	if((x == barrier_end && p0_momentum > 0) || (x == resolution_x - barrier_end && p1_momentum < 0) || x == 0 || in_paddle(x - 1, y) || in_paddle(x, y)){
		x0 = 0.0;
	} else {
		x0 = vector[x - 1][y];
	}
	x1 = vector[x][y];
	if((x == barrier_end - 1 && p0_momentum > 0) || (x == resolution_x - barrier_end - 1 && p1_momentum < 0) || x == resolution_x - 1 || in_paddle(x + 1, y) || in_paddle(x, y)){
		x2 = 0.0;
	} else {
		x2 = vector[x + 1][y];
	}

	if(y == 0 || in_paddle(x, y - 1) || in_paddle(x, y)){
		y0 = 0.0;
	} else {
		y0 = vector[x][y - 1];
	}
	y1 = vector[x][y];
	if(y == resolution_y - 1 || in_paddle(x, y + 1) || in_paddle(x, y)){
		y2 = 0.0;
	} else {
		y2 = vector[x][y + 1];
	}

	*out_x = x0 - 2.0*x1 + x2;
	*out_y = y0 - 2.0*y1 + y2;
}

void simulate(double dt){
	int x, y;
	double second_derivative_imag_x, second_derivative_imag_y;
	double second_derivative_real_x, second_derivative_real_y;
	double potential, prev_p0_round_score, prev_p1_round_score;

	for(x = 0; x < resolution_x; x++){
		for(y = 0; y < resolution_y; y++){
			potential = 0.0;
			get_second_derivative(&second_derivative_imag_x, &second_derivative_imag_y, state_imag, x, y, state_real, state_imag);
			next_state_real[x][y] = state_real[x][y] + second_derivative_imag_x*dt + second_derivative_imag_y*dt + potential*state_imag[x][y]*dt;
		}
	}
	
	prev_p0_round_score = p0_round_score;
	prev_p1_round_score = p1_round_score;
	p0_round_score = 0.0;
	p1_round_score = 0.0;
	for(x = 0; x < resolution_x; x++){
		for(y = 0; y < resolution_y; y++){
			potential = 0.0;
			get_second_derivative(&second_derivative_real_x, &second_derivative_real_y, next_state_real, x, y, next_state_real, state_imag);
			next_state_imag[x][y] = state_imag[x][y] - second_derivative_real_x*dt - second_derivative_real_y*dt - potential*next_state_real[x][y]*dt;

			if(x < barrier_end){
				p1_round_score += next_state_real[x][y]*next_state_real[x][y] + next_state_imag[x][y]*next_state_imag[x][y];
			}
			if(x >= resolution_x - barrier_end){
				p0_round_score += next_state_real[x][y]*next_state_real[x][y] + next_state_imag[x][y]*next_state_imag[x][y];
			}
		}
	}

	if(p0_round_score < prev_p0_round_score){
		p0_round_score = prev_p0_round_score;
	}
	if(p1_round_score < prev_p1_round_score){
		p1_round_score = prev_p1_round_score;
	}

	normalize(next_state_real, next_state_imag, state_imag);
}

void handle_input(double dt){
	if(IsKeyDown(KEY_UP)){
		paddle1_pos -= paddle_speed*dt*target_fps;
		if(paddle1_pos < 0.0){
			paddle1_pos = 0.0;
		}
	}
	if(IsKeyDown(KEY_DOWN)){
		paddle1_pos += paddle_speed*dt*target_fps;
		if(paddle1_pos + paddle_size > resolution_y){
			paddle1_pos = resolution_y - paddle_size;
		}
	}
	if(IsKeyDown(KEY_LEFT_SHIFT)){
		paddle0_pos -= paddle_speed*dt*target_fps;
		if(paddle0_pos < 0.0){
			paddle0_pos = 0.0;
		}
	}
	if(IsKeyDown(KEY_LEFT_CONTROL)){
		paddle0_pos += paddle_speed*dt*target_fps;
		if(paddle0_pos + paddle_size > resolution_y){
			paddle0_pos = resolution_y - paddle_size;
		}
	}
}

void welcome_message(void){
	int cont = 1, key;

	const char *string0 = "Player 1 Controls: Ctrl, Shift";
	const char *string1 = "Player 2 Controls: Up, Down";
	const char *string2 = "Press enter to begin.";
	Vector2 text0_size, text1_size;
	text0_size = MeasureTextEx(GetFontDefault(), string0, font_size, font_size/10);
	text1_size = MeasureTextEx(GetFontDefault(), string1, font_size, font_size/10);
	while(cont){
		BeginDrawing();
		ClearBackground(BLACK);
		DrawTextEx(GetFontDefault(), string0, (Vector2) {.x = 0.0, .y = 0.0}, font_size, font_size/10, (Color) {.r = 255, .g = 255, .b = 255, .a = 255});
		DrawTextEx(GetFontDefault(), string1, (Vector2) {.x = 0.0, .y = text0_size.y + font_size/5.0}, font_size, font_size/10, (Color) {.r = 255, .g = 255, .b = 255, .a = 255});
		DrawTextEx(GetFontDefault(), string2, (Vector2) {.x = 0.0, .y = text0_size.y + text1_size.y + 2.0*font_size/5}, font_size, font_size/10, (Color) {.r = 255, .g = 255, .b = 255, .a = 255});
		EndDrawing();
		if(IsKeyDown(KEY_ENTER)){
			cont = 0;
		}
	}
}

int main(int argc, char **argv){
	Image canvas;
	Texture2D texture;
	int k;
	double frame_time = 0.0;

	pixels = malloc(sizeof(uint8_t)*resolution_x*resolution_y*4);
	SetConfigFlags(FLAG_VSYNC_HINT);
	InitWindow(1, 1, "Quantum Pong");

	if(!IsWindowReady()){
		fprintf(stderr, "Error: failed to open window.\n");
		return 1;
	}
	SetWindowState(FLAG_WINDOW_RESIZABLE);
	MaximizeWindow();

	SetTargetFPS(target_fps);
	canvas = GenImageColor(resolution_x, resolution_y, BLACK);
	ImageFormat(&canvas, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	texture = LoadTextureFromImage(canvas);

	//welcome_message();
	start_new_round();

	while(!WindowShouldClose()){
		handle_input(frame_time);
		if(current_time - round_start_time > 3.0){
			for(k = 0; k < ticks_per_frame; k++){
				simulate(frame_time*time_step);
				memcpy(state_real, next_state_real, sizeof state_real);
				memcpy(state_imag, next_state_imag, sizeof state_imag);
			}
		}
		render(&texture);
		frame_time = GetFrameTime();
		if(frame_time > 2.0/target_fps){
			frame_time = 2.0/target_fps;
		}
		current_time = GetTime();
		if((p0_round_score > 0.4 || p1_round_score > 0.4) && critical_mass_time < 0.0){
			critical_mass_time = current_time;
		}
		if(current_time - round_start_time > max_round_time || (critical_mass_time > 0.0 && current_time - critical_mass_time > 5.0)){
			start_new_round();
		}
	}

	UnloadImage(canvas);
	UnloadTexture(texture);
	CloseWindow();
	free(pixels);

	return 0;
}

