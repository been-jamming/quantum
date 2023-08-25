#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <complex.h>

#define HAVE_INLINE

#include <gsl/gsl_math.h>
#include <gsl/gsl_complex_math.h>
#include <gsl/gsl_eigen.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_linalg.h>
#include <raylib.h>

unsigned int resolution;
double mass;
gsl_matrix_complex *H;
gsl_matrix_complex *P;
gsl_matrix_complex *M;
gsl_matrix_complex *H_momentum;
gsl_matrix_complex *V;

gsl_matrix_complex *FT;
gsl_matrix_complex *IFT;

gsl_matrix_complex *H_eigenvectors;
gsl_vector *H_eigenvalues;
gsl_vector_complex *state;
gsl_vector_complex *initial_state_eigenbasis;
gsl_vector_complex *state_eigenbasis;

int screen_width;
int screen_height;

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
	for(i = 0; i < (resolution - 1)/2; i++){
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

	//Set H to H_momentum
	gsl_matrix_complex_memcpy(H, H_momentum);

	//Compute the eigenvalues and eigenvectors of H
	H_eigenvalues = gsl_vector_alloc(resolution);
	H_eigenvectors = gsl_matrix_complex_alloc(resolution, resolution);
	w = gsl_eigen_hermv_alloc(resolution);
	gsl_eigen_hermv(H, H_eigenvalues, H_eigenvectors, w);
	gsl_eigen_hermv_free(w);

	//Initialize the state!
	state = gsl_vector_complex_alloc(resolution);
	for(i = 0; i < resolution; i++){
		len = 1.0/(1 + fabs((double) i/resolution - 0.5));
		gsl_vector_complex_set(state, i, len*len*len*len*len*len*len*len*len*len*gsl_complex_exp(-2*M_PI*I*i*5/resolution));
	}
	
	//Normalize the state
	gsl_blas_zdotc(state, state, &length);
	gsl_vector_complex_scale(state, 1.0/csqrt(length));

	//Change the basis of the initial state to the basis of eigenvectors
	initial_state_eigenbasis = gsl_vector_complex_alloc(resolution);
	gsl_blas_zgemv(CblasConjTrans, 1.0, H_eigenvectors, state, 0.0, initial_state_eigenbasis);

	//Initialize memory for computing the new state's components in the eigenbasis
	state_eigenbasis = gsl_vector_complex_alloc(resolution);
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

void render_state(int pos_x, int pos_y, int width, int height){
	unsigned int i;
	complex double entry;
	double phase;
	double red;
	double green;
	double blue;
	double abs2;
	double largest_abs2 = 0.0;
	Color rect_color;
	int last_x_pos;

	for(i = 0; i < resolution; i++){
		entry = gsl_vector_complex_get(state, i);
		abs2 = gsl_complex_abs2(entry);
		if(abs2 > largest_abs2){
			largest_abs2 = abs2;
		}
	}

	DrawRectangle(pos_x, pos_y, width, height, BLACK);
	last_x_pos = pos_x;
	for(i = 0; i < resolution; i++){
		entry = gsl_vector_complex_get(state, i);
		phase = gsl_complex_arg(entry) + M_PI;
		phase_to_color(phase, &red, &green, &blue);
		abs2 = gsl_complex_abs2(entry);
		rect_color = (Color) {.r = red*255, .g = green*255, .b = blue*255, .a = 255};
		//DrawRectangle(pos_x + width*i/resolution, pos_y + height*(1.0 - abs2/largest_abs2), width*(i + 1)/resolution - width*i/resolution, height*abs2/largest_abs2, rect_color);
		DrawRectangle(pos_x + width*i/resolution, pos_y + height*(1.0 - abs2*sqrt(resolution)), width*(i + 1)/resolution - width*i/resolution, height*abs2*sqrt(resolution), rect_color);
		//DrawRectangle(pos_x + width*i/resolution, pos_y + height*(1.0 - abs2), width*(i + 1)/resolution - width*i/resolution, height*abs2, rect_color);
	}
}

int main(int argc, char **argv){
	Vector2 text_size;
	Font default_font;
	int text_x_pos;
	int text_y_pos;
	double time;
	double time_scale = 0.03;

	resolution = 301;
	mass = 1.0;
	time = 0.0;

	InitWindow(480, 600, "Particle in Ring");
	if(!IsWindowReady()){
		fprintf(stderr, "Error: failed to open window\n");
		return 1;
	}
	if(!IsWindowFullscreen()){
		ToggleFullscreen();
	}

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
		compute_state(time);
		BeginDrawing();
		ClearBackground(WHITE);
		render_state(screen_width/5, screen_height/5, 3*screen_width/5, 3*screen_height/5);
		EndDrawing();
		time += 1.0/60.0*time_scale;
	}

	return 0;
}

