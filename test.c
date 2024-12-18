#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <complex.h>

#define HAVE_INLINE
#define EPSILON 0.000001

#include <gsl/gsl_math.h>
#include <gsl/gsl_complex_math.h>
#include <gsl/gsl_eigen.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_linalg.h>
#include <raylib.h>

const unsigned int POSITION = 1;
const unsigned int MOMENTUM = 2;
const unsigned int POTENTIAL = 4;
const unsigned int PAUSED = 8;
const unsigned int OBSERVABLE = 16;
const unsigned int WATCH = 32;

unsigned char potential_edited = 0;

double max_potential = 4.0;
double time_scale = 1.0;

char message[64] = {0};
char typed[64] = {0};
int cursor;

unsigned int ui_mode = POSITION | PAUSED;

unsigned int resolution;
double mass;
gsl_matrix_complex *H;
gsl_matrix_complex *P;
gsl_matrix_complex *M;
gsl_matrix_complex *H_momentum;
gsl_matrix_complex *V;
gsl_matrix_complex *W;
gsl_matrix_complex *W2;

gsl_matrix_complex *FT;
gsl_matrix_complex *IFT;

gsl_matrix_complex *H_eigenvectors;
gsl_vector *H_eigenvalues;
gsl_vector_complex *state;
gsl_vector_complex *state_momentum;
gsl_vector_complex *initial_state_eigenbasis;
gsl_vector_complex *state_eigenbasis;
gsl_vector_complex *Wv;

int screen_width;
int screen_height;
double time;

int compute_observable(char *str);

void recompute_state(void){
	complex double length;

	//Change the basis of the initial state to the basis of eigenvectors
	gsl_blas_zgemv(CblasConjTrans, 1.0, H_eigenvectors, state, 0.0, initial_state_eigenbasis);
	time = 0.0;
}

void recompute_hamiltonian(void){
	gsl_eigen_hermv_workspace *w;

	gsl_matrix_complex_memcpy(H, H_momentum);
	gsl_matrix_complex_add(H, V);

	//Compute the eigenvalues and eigenvectors of H
	w = gsl_eigen_hermv_alloc(resolution);
	gsl_eigen_hermv(H, H_eigenvalues, H_eigenvectors, w);
	gsl_eigen_hermv_free(w);

	recompute_state();
}

void initialize(void){
	int i;
	int j;
	double momentum;
	complex double length;
	double len;
	gsl_eigen_hermv_workspace *w;
	gsl_permutation *p;

	//Initialize the matrix for the fourier transform
	FT = gsl_matrix_complex_alloc(resolution, resolution);
	for(i = 0; i < resolution; i++){
		for(j = 0; j < resolution; j++){
			gsl_matrix_complex_set(FT, i, j, gsl_complex_exp(-2*M_PI*I*i*j/resolution)/sqrt(resolution));
		}
	}
	//Initialize the matrix for the inverse fourier transform
	IFT = gsl_matrix_complex_alloc(resolution, resolution);
	for(i = 0; i < resolution; i++){
		for(j = 0; j < resolution; j++){
			gsl_matrix_complex_set(IFT, i, j, gsl_complex_exp(2*M_PI*I*i*j/resolution)/sqrt(resolution));
		}
	}

	//Initialize the momentum operator in the momentum basis
	M = gsl_matrix_complex_alloc(resolution, resolution);
	gsl_matrix_complex_set_zero(M);
	for(i = 0; i <= (resolution - 1)/2; i++){
		momentum = i;
		gsl_matrix_complex_set(M, i, i, momentum);
	}
	for(i = (resolution + 1)/2; i < resolution; i++){
		momentum = resolution - i;
		gsl_matrix_complex_set(M, i, i, momentum);
	}

	//Multiply M and FT, store to H
	H = gsl_matrix_complex_alloc(resolution, resolution);
	gsl_blas_zgemm(CblasNoTrans, CblasNoTrans, 1.0, M, FT, 0.0, H);

	//Multiply IFT and H, store to P
	P = gsl_matrix_complex_alloc(resolution, resolution);
	gsl_blas_zgemm(CblasNoTrans, CblasNoTrans, 1.0, IFT, H, 0.0, P);

	//Set H_momentum to P^2/(2m)
	H_momentum = gsl_matrix_complex_alloc(resolution, resolution);
	gsl_blas_zgemm(CblasNoTrans, CblasNoTrans, 1.0/(2*mass), P, P, 0.0, H_momentum);

	//Initialize V
	V = gsl_matrix_complex_alloc(resolution, resolution);
	gsl_matrix_complex_set_zero(V);

	//Set H to H_momentum
	gsl_matrix_complex_memcpy(H, H_momentum);

	//Compute the eigenvalues and eigenvectors of H
	H_eigenvalues = gsl_vector_alloc(resolution);
	H_eigenvectors = gsl_matrix_complex_alloc(resolution, resolution);
	w = gsl_eigen_hermv_alloc(resolution);
	gsl_eigen_hermv(H, H_eigenvalues, H_eigenvectors, w);
	gsl_eigen_hermv_free(w);

	//Initialize the vector which stores the state in the momentum basis
	state_momentum = gsl_vector_complex_alloc(resolution);

	//Initialize the state!
	state = gsl_vector_complex_alloc(resolution);
	for(i = 0; i < resolution; i++){
		len = 1.0/(1 + fabs((double) i/resolution - 0.5));
		gsl_vector_complex_set(state, i, len*len*len*len*len*len*len*len*len*len*gsl_complex_exp(-2*M_PI*I*i*2/resolution));
	}
	
	//Initialize memory for computing the new state's components in the eigenbasis
	state_eigenbasis = gsl_vector_complex_alloc(resolution);

	//Initialize memory for computing the initial state in the eigenbasis
	initial_state_eigenbasis = gsl_vector_complex_alloc(resolution);

	//Initialize memory for observable
	W = gsl_matrix_complex_alloc(resolution, resolution);
	W2 = gsl_matrix_complex_alloc(resolution, resolution);
	Wv = gsl_vector_complex_alloc(resolution);

	//Normalize the state
	gsl_blas_zdotc(state, state, &length);
	gsl_vector_complex_scale(state, 1.0/csqrt(length));

	//Normalize the state
	gsl_blas_zdotc(state, state, &length);
	gsl_vector_complex_scale(state, 1.0/csqrt(length));

	//Preprocess the state
	recompute_state();
}

void compute_state(double time){
	unsigned int i;
	complex double entry;
	double energy;
	complex double coefficient;

	for(i = 0; i < resolution; i++){
		entry = gsl_vector_complex_get(initial_state_eigenbasis, i);
		energy = gsl_vector_get(H_eigenvalues, i);
		coefficient = entry*gsl_complex_exp(energy*time*I);
		gsl_vector_complex_set(state_eigenbasis, i, coefficient);
	}

	gsl_blas_zgemv(CblasNoTrans, 1.0, H_eigenvectors, state_eigenbasis, 0.0, state);
}

void phase_to_color(double phase, double *red, double *green, double *blue){
	Color output;

	if(phase <= M_PI/3 || phase >= 5*M_PI/3){
		*red = 1.0;
		if(phase <= M_PI/3){
			*green = phase*3/M_PI;
			*blue = 0.0;
		} else {
			*green = 0.0;
			*blue = 1.0 - (phase - 5*M_PI/3)*3/M_PI;
		}
	} else if(phase >= M_PI/3 && phase <= M_PI){
		*green = 1.0;
		if(phase <= 2*M_PI/3){
			*red = (2*M_PI/3 - phase)*3/M_PI;
			*blue = 0.0;
		} else {
			*red = 0.0;
			*blue = 1.0 - (M_PI - phase)*3/M_PI;
		}
	} else {
		*blue = 1;
		if(phase <= 4*M_PI/3){
			*red = 0.0;
			*green = (4*M_PI/3 - phase)*3/M_PI;
		} else {
			*red = 1.0 - (5*M_PI/3 - phase)*3/M_PI;
			*green = 0.0;
		}
	}
}

double render_state_momentum(double largest_abs2, int pos_x, int pos_y, int width, int height){
	unsigned int i;
	unsigned int j;
	complex double entry;
	double phase;
	double red;
	double green;
	double blue;
	double abs2;
	Color rect_color;

	gsl_blas_zgemv(CblasNoTrans, 1.0, FT, state, 0.0, state_momentum);

	if(largest_abs2 < 0.0 || !(ui_mode&PAUSED)){
		largest_abs2 = 0.0;
		for(i = 0; i < resolution; i++){
			entry = gsl_vector_complex_get(state_momentum, i);
			abs2 = gsl_complex_abs2(entry);
			if(abs2 > largest_abs2){
				largest_abs2 = abs2;
			}
		}

		if(largest_abs2 < 1.0/sqrt(resolution)){
			largest_abs2 = 1.0/sqrt(resolution);
		}
	}

	for(i = (resolution - 1)/2, j = 0; i < resolution; i++, j++){
		entry = gsl_vector_complex_get(state_momentum, j);
		phase = gsl_complex_arg(entry) + M_PI;
		phase_to_color(phase, &red, &green, &blue);
		abs2 = gsl_complex_abs2(entry);
		rect_color = (Color) {.r = red*255, .g = green*255, .b = blue*255, .a = 255};
		DrawRectangle(pos_x + width*i/resolution, pos_y + height*(1.0 - abs2/largest_abs2), width*(i + 1)/resolution - width*i/resolution, height*abs2/largest_abs2, rect_color);
	}

	for(i = 0, j = (resolution + 1)/2; i < (resolution - 1)/2; i++, j++){
		entry = gsl_vector_complex_get(state_momentum, j);
		phase = gsl_complex_arg(entry) + M_PI;
		phase_to_color(phase, &red, &green, &blue);
		abs2 = gsl_complex_abs2(entry);
		rect_color = (Color) {.r = red*255, .g = green*255, .b = blue*255, .a = 255};
		DrawRectangle(pos_x + width*i/resolution, pos_y + height*(1.0 - abs2/largest_abs2), width*(i + 1)/resolution - width*i/resolution, height*abs2/largest_abs2, rect_color);
	}

	return largest_abs2;
}

double render_state_position(double largest_abs2, int pos_x, int pos_y, int width, int height){
	unsigned int i;
	complex double entry;
	double phase;
	double red;
	double green;
	double blue;
	double abs2;
	Color rect_color;

	if(largest_abs2 < 0.0 || !(ui_mode&PAUSED)){
		largest_abs2 = 0.0;
		for(i = 0; i < resolution; i++){
			entry = gsl_vector_complex_get(state, i);
			abs2 = gsl_complex_abs2(entry);
			if(abs2 > largest_abs2){
				largest_abs2 = abs2;
			}
		}

		if(largest_abs2 < 1.0/sqrt(resolution)){
			largest_abs2 = 1.0/sqrt(resolution);
		}
	}

	for(i = 0; i < resolution; i++){
		entry = gsl_vector_complex_get(state, i);
		phase = gsl_complex_arg(entry) + M_PI;
		phase_to_color(phase, &red, &green, &blue);
		abs2 = gsl_complex_abs2(entry);
		rect_color = (Color) {.r = red*255, .g = green*255, .b = blue*255, .a = 255};
		DrawRectangle(pos_x + width*i/resolution, pos_y + height*(1.0 - abs2/largest_abs2), width*(i + 1)/resolution - width*i/resolution, height*abs2/largest_abs2, rect_color);
	}

	return largest_abs2;
}

void render_potential(double max_potential, int pos_x, int pos_y, int width, int height){
	unsigned int i;
	complex double entry;
	double abs;
	
	for(i = 0; i < resolution; i++){
		entry = gsl_matrix_complex_get(V, i, i);
		abs = gsl_complex_abs(entry);
		if(abs > max_potential){
			abs = max_potential;
		}
		DrawRectangle(pos_x + width*i/resolution, pos_y + height*(1.0 - abs/max_potential), width*(i + 1)/resolution - width*i/resolution, height*abs/max_potential, GRAY);
	}
}

double edit_position(double max_val, int pos_x, int pos_y, int width, int height){
	int mouse_x;
	int mouse_y;
	Vector2 delta;
	complex double entry;
	int index;
	double value;
	complex double length;

	delta = GetMouseDelta();
	mouse_x = GetMouseX();
	mouse_y = GetMouseY();
	if((ui_mode&PAUSED) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (delta.x != 0 || delta.y != 0) && mouse_x >= pos_x && mouse_y >= pos_y && mouse_x < pos_x + width && mouse_y < pos_y + height){
		index = (mouse_x - pos_x)*resolution/width;
		if(index < 0){
			index = 0;
		}
		if(index >= resolution){
			index = resolution - 1;
		}
		value = 1.0 - (double) (mouse_y - pos_y)/height;
		entry = gsl_vector_complex_get(state, index);
		entry = entry*sqrt(value*max_val)/cabs(entry);
		snprintf(message, 64, "Editing position %d to norm %.2f", index, sqrt(value*max_val));
		gsl_vector_complex_set(state, index, entry);

		recompute_state();
	}
}

double edit_momentum(double max_val, int pos_x, int pos_y, int width, int height){
	int mouse_x;
	int mouse_y;
	Vector2 delta;
	complex double entry;
	int index;
	int display_index;
	double value;
	complex double length;

	delta = GetMouseDelta();
	mouse_x = GetMouseX();
	mouse_y = GetMouseY();
	if((ui_mode&PAUSED) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (delta.x != 0 || delta.y != 0) && mouse_x >= pos_x && mouse_y >= pos_y && mouse_x < pos_x + width && mouse_y < pos_y + height){
		index = (mouse_x - pos_x)*resolution/width;
		if(index < (resolution - 1)/2){
			display_index = index - (resolution - 1)/2;
			index += (resolution + 1)/2;
		} else {
			index -= (resolution - 1)/2;
			display_index = index;
		}
		value = 1.0 - (double) (mouse_y - pos_y)/height;
		entry = gsl_vector_complex_get(state_momentum, index);
		entry = entry*sqrt(value*max_val)/cabs(entry);
		snprintf(message, 64, "Editing momentum %d to norm %.2f", display_index, sqrt(value*max_val));
		gsl_vector_complex_set(state_momentum, index, entry);
		gsl_blas_zgemv(CblasNoTrans, 1.0, IFT, state_momentum, 0.0, state);

		recompute_state();
	}
}

void edit_potential(double max_potential, int pos_x, int pos_y, int width, int height){
	int mouse_x;
	int mouse_y;
	Vector2 delta;
	complex double entry;
	int index;
	double value;

	delta = GetMouseDelta();
	mouse_x = GetMouseX();
	mouse_y = GetMouseY();
	if((ui_mode&PAUSED) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (delta.x != 0 || delta.y != 0) && mouse_x >= pos_x && mouse_y >= pos_y && mouse_x < pos_x + width && mouse_y < pos_y + height){
		potential_edited = 1;
		index = (mouse_x - pos_x)*resolution/width;
		value = 1.0 - (double) (mouse_y - pos_y)/height;
		entry = gsl_matrix_complex_get(V, index, index);
		entry = value*max_potential;
		snprintf(message, 64, "Editing potential %d to %.2f", index, value*max_potential);
		gsl_matrix_complex_set(V, index, index, entry);
	}
}

void handle_input(double *pos_max_val, double *mom_max_val){
	int key;
	int i;
	int err;
	complex double length;

	while((key = GetCharPressed())){
		if((ui_mode&OBSERVABLE) == 0){
			switch(key){
				case ' ':
					ui_mode ^= PAUSED;
					if(ui_mode&PAUSED){
						snprintf(message, 64, "Paused");
					} else {
						snprintf(message, 64, "Unpaused");
						//Normalize the state
						gsl_blas_zdotc(state, state, &length);
						gsl_vector_complex_scale(state, 1.0/csqrt(length));
						if(potential_edited){
							recompute_hamiltonian();
							potential_edited = 0;
						} else {
							recompute_state();
						}
					}
					break;
				case 'p':
					ui_mode &= ~0x07;
					ui_mode |= MOMENTUM;
					snprintf(message, 64, "Momentum");
					break;
				case 'x':
					ui_mode &= ~0x07;
					ui_mode |= POSITION;
					snprintf(message, 64, "Position");
					break;
				case 'v':
					ui_mode &= ~0x07;
					ui_mode |= POTENTIAL;
					snprintf(message, 64, "Potential");
					break;
				case '=':
					max_potential *= 2;
					snprintf(message, 64, "Max potential: %g", max_potential);
					break;
				case '-':
					max_potential /= 2;
					snprintf(message, 64, "Max potential: %g", max_potential);
					break;
				case ',':
					time_scale /= 2;
					snprintf(message, 64, "Speed: %g", time_scale);
					break;
				case '.':
					time_scale *= 2;
					snprintf(message, 64, "Speed: %g", time_scale);
					break;
				case 'z':
					if(((ui_mode&POSITION) || (ui_mode&MOMENTUM))&&(ui_mode&PAUSED)){
						snprintf(message, 64, "Set state to 0");
						for(i = 0; i < resolution; i++){
							gsl_vector_complex_set(state, i, EPSILON);
						}
						recompute_state();

						*pos_max_val = 1.0;
						*mom_max_val = 1.0;
					} else if(ui_mode&POTENTIAL){
						snprintf(message, 64, "Set potential to 0");
						for(i = 0; i < resolution; i++){
							gsl_matrix_complex_set(V, i, i, 0);
						}
						recompute_hamiltonian();
					}
					break;
				case 'm':
					ui_mode |= OBSERVABLE | PAUSED;
					break;
			}
		} else {
			if(' ' <= key && key <= 125 && key != '\\'){
				typed[cursor] = key;
				if(cursor < 62){
					cursor++;
				}
			}
			snprintf(message, 64, "%s_", typed);
			if(key == '\\'){
				err = compute_observable(typed);
				gsl_blas_zgemm(CblasNoTrans, CblasNoTrans, 1.0, W, W, 0.0, W2);
				if(err){
					snprintf(message, 64, "Error");
				}
				for(i = 0; i < 64; i++){
					typed[i] = '\0';
					cursor = 0;
				}
				ui_mode &= ~OBSERVABLE;
				ui_mode &= PAUSED;
				ui_mode |= WATCH;
			}
		}
	}
}

void center_message(void){
	Vector2 text_size;
	Font default_font;
	int text_x_pos;
	int text_y_pos;

	default_font = GetFontDefault();
	text_size = MeasureTextEx(default_font, message, screen_height/15.0, 1.0);
	text_x_pos = (screen_width - text_size.x)/2;
	text_y_pos = screen_height*9/10 - text_size.y/2;
	DrawText(message, text_x_pos, text_y_pos, screen_height/15.0, BLACK);
}

void skip_whitespace(char **c){
	while(**c == ' ' || **c == '\t'){
		++*c;
	}
}

int compute_value(char **c, gsl_matrix_complex *R);

int compute_observable_recursive(char **c, gsl_matrix_complex *R, int precedence){
	gsl_matrix_complex *A;
	gsl_matrix_complex *B;
	char operation;
	int err;
	int i;
	int j;

	A = gsl_matrix_complex_alloc(resolution, resolution);
	B = gsl_matrix_complex_alloc(resolution, resolution);
	skip_whitespace(c);
	err = compute_value(c, R);
	skip_whitespace(c);
	while(!err && **c && **c != ')'){
		operation = **c;
		++*c;
		skip_whitespace(c);
		if(!precedence && operation == '*'){
			err = err || compute_observable_recursive(c, A, 1);
		} else if(precedence && operation != '*'){
			--*c;
			break;
		} else {
			err = err || compute_value(c, A);
		}
		skip_whitespace(c);
		switch(operation){
			case '+':
				for(i = 0; i < resolution; i++){
					for(j = 0; j < resolution; j++){
						gsl_matrix_complex_set(B, i, j, gsl_matrix_complex_get(R, i, j) + gsl_matrix_complex_get(A, i, j));
					}
				}
				break;
			case '-':
				for(i = 0; i < resolution; i++){
					for(j = 0; j < resolution; j++){
						gsl_matrix_complex_set(B, i, j, gsl_matrix_complex_get(R, i, j) - gsl_matrix_complex_get(A, i, j));
					}
				}
				break;
			case '*':
				gsl_blas_zgemm(CblasNoTrans, CblasNoTrans, 1.0, R, A, 0.0, B);
				break;
		}
		gsl_matrix_complex_memcpy(R, B);
	}
	gsl_matrix_complex_free(A);
	gsl_matrix_complex_free(B);

	return err;
}

int compute_value(char **c, gsl_matrix_complex *R){
	int i;
	int j;

	if(**c == '('){
		++*c;
		compute_observable_recursive(c, R, 0);
		if(**c != ')'){
			return 1;
		}
		++*c;
	} else if(**c == 'X'){
		++*c;
		for(i = 0; i < resolution; i++){
			for(j = 0; j < resolution; j++){
				gsl_matrix_complex_set(R, i, j, (complex) i == j);
			}
		}
	} else if(**c == 'V'){
		++*c;
		gsl_matrix_complex_memcpy(R, V);
	} else if(**c == 'P'){
		++*c;
		gsl_matrix_complex_memcpy(R, P);
	} else if(**c == 'H'){
		++*c;
		gsl_matrix_complex_memcpy(R, H);
	} else {
		return 1;
	}

	return 0;
}

int compute_observable(char *str){
	return compute_observable_recursive(&str, W, 0);
}

int main(int argc, char **argv){
	Vector2 text_size;
	Font default_font;
	int text_x_pos;
	int text_y_pos;
	double pos_max_val = -1.0;
	double mom_max_val = -1.0;
	complex double ev;

	resolution = 101;
	mass = 1.0;
	time = 0.0;

	SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
	InitWindow(0, 0, "Particle in Ring");

	if(!IsWindowReady()){
		fprintf(stderr, "Error: failed to open window\n");
		return 1;
	}
	if(!IsWindowMaximized()){
		MaximizeWindow();
	}

	SetWindowState(FLAG_WINDOW_RESIZABLE);

	BeginDrawing();
	ClearBackground(BLACK);
	EndDrawing();

	screen_width = GetRenderWidth();
	screen_height = GetRenderHeight();
	default_font = GetFontDefault();

	text_size = MeasureTextEx(default_font, "Loading...", screen_height/10.0, 1.0);
	text_x_pos = (screen_width - text_size.x)/2;
	text_y_pos = (screen_height - text_size.y)/2;

	BeginDrawing();
	ClearBackground(BLACK);
	DrawText("Loading...", text_x_pos, text_y_pos, screen_height/10.0, WHITE);
	EndDrawing();

	initialize();

	while(!WindowShouldClose()){
		screen_width = GetRenderWidth();
		screen_height = GetRenderHeight();
		compute_state(time);
		BeginDrawing();
		ClearBackground(WHITE);
		if(ui_mode&WATCH){
			gsl_blas_zgemv(CblasNoTrans, 1.0, W, state, 0.0, Wv);
			gsl_blas_zdotc(Wv, state, &ev);
			snprintf(message, 64, "EV: %lf", creal(ev));
		}
		center_message();
		DrawRectangle(screen_width/5, screen_height/5, 3*screen_width/5, 3*screen_height/5, BLACK);
		if(ui_mode&POSITION){
			render_potential(max_potential, screen_width/5, screen_height/5, 3*screen_width/5, 3*screen_height/5);
			pos_max_val = render_state_position(pos_max_val, screen_width/5, screen_height/5, 3*screen_width/5, 3*screen_height/5);
			edit_position(pos_max_val, screen_width/5, screen_height/5, 3*screen_width/5, 3*screen_height/5);
		} else if(ui_mode&MOMENTUM){
			mom_max_val = render_state_momentum(mom_max_val, screen_width/5, screen_height/5, 3*screen_width/5, 3*screen_height/5);
			edit_momentum(mom_max_val, screen_width/5, screen_height/5, 3*screen_width/5, 3*screen_height/5);
		} else if(ui_mode&POTENTIAL){
			pos_max_val = render_state_position(pos_max_val, screen_width/5, screen_height/5, 3*screen_width/5, 3*screen_height/5);
			render_potential(max_potential, screen_width/5, screen_height/5, 3*screen_width/5, 3*screen_height/5);
			if(ui_mode&PAUSED){
				edit_potential(max_potential, screen_width/5, screen_height/5, 3*screen_width/5, 3*screen_height/5);
			}
		}
		EndDrawing();
		handle_input(&pos_max_val, &mom_max_val);
		if(!(ui_mode&PAUSED)){
			time += GetFrameTime()*time_scale;
		}
	}

	return 0;
}

