#include <cassert>

#include "pencil_drawing.h"
#include "math_utils.h"

using namespace lime;

const double tau = M_PI / 6.0;
const int t_disc = 24;
const int s_disc = 24;
const double sigma_1 = 4.0;
const double sigma_2 = 2.0;
const double S = 7.0;
const int n = 2;
const int ksize = 11;

void showProgress(int current, int total) {
	double percent = (double)current / total;
	int n_prog = (int)(30.0 * percent);
	printf(" %5.1f [", 100.0 * percent);
	for(int i=1; i<=30; i++) {
		if(i == n_prog) printf(">");
		else i < n_prog ? printf("=") : printf(" ");
	}
	printf("]\r");

	if(current == total) printf("\n");
}

void quantizeOrientation(cv::Mat& vfield, cv::Mat& edge) {
	int width = vfield.cols;
	int height = vfield.rows;
	double threshold = std::max(width, height) / 50.0;
	
	double q_s = -M_PI / 4.0;
	double q_t = M_PI;
	cv::Mat uedge;
	edge.convertTo(uedge, CV_8UC1, 255.0);
	for(int y=0; y<height; y++) {
		for(int x=0; x<width; x++) {
			if(uedge.at<uchar>(y, x) < 192) {
				uedge.at<uchar>(y, x) = 0;
			} else {
				uedge.at<uchar>(y, x) = 1;
			}
		}
	}

	cv::Mat distmap = cv::Mat(height, width, CV_32FC1);
	cv::distanceTransform(uedge, distmap, cv::DIST_L2, 3);
	for(int y=0; y<height; y++) {
		for(int x=0; x<width; x++) {
			if(distmap.at<float>(y, x) > threshold) {
				float theta = vfield.at<float>(y, x);
				vfield.at<float>(y, x) = (float)(ceil(theta / q_t) * q_t + q_s);
			}
		}
	}
}

void LIConv(cv::Mat& lic, cv::Mat& img, cv::Mat& etf, cv::Mat& noise, double ratio) {
	int width = img.cols;
	int height = img.rows;
	int dim = img.channels();
	assert(width == etf.cols && height == etf.rows && etf.channels() == 1 && etf.depth() == CV_32F);
	assert(width == noise.cols && height == noise.rows && noise.channels() == 1 && noise.depth() == CV_32F);

	cv::Mat bilateral;
	cv::bilateralFilter(img, bilateral, 19, 0.5, 15.0);

	lic = cv::Mat(height, width, img.type());
	int totalStep = dim * height * width;
	int progress = 0;

	lime::Random rand = lime::Random::getRNG();

	for(int y=0; y<height; y++) {
		ompfor (int x=0; x<width; x++) {
			std::vector<double> s_sum(dim, 0.0);
			std::vector<double> w_sum(dim, 0.0);
			std::vector<double> sum(dim, 0.0);
			std::vector<double> weight(dim, 0.0);

			double t_i = tau / n * (rand.randInt(2*n+1) - n);
			for(int t=-t_disc; t<=t_disc; t++) {
				double theta = tau / t_disc * t +  etf.at<float>(y, x);
				fill(s_sum.begin(), s_sum.end(), 0.0);
				fill(w_sum.begin(), w_sum.end(), 0.0);
				for(int s=-s_disc; s<=s_disc; s++) {
					double scale = S / s_disc * s;
					int xx = (int)(x + scale * cos(theta));
					int yy = (int)(y + scale * sin(theta));
					if(xx >= 0 && yy >= 0 && xx < width && yy < height) {
						for(int c=0; c<dim; c++) {
							double dI = bilateral.at<float>(yy, xx*dim+c) - bilateral.at<float>(y, x*dim+c);
							double G2 = gauss(dI, sigma_2);
							s_sum[c] += G2 * noise.at<float>(yy, xx) * (1.0 - bilateral.at<float>(y, x*dim+c));
							w_sum[c] += G2;
						}
					}
				}
				double G1 = gauss(t_i - theta, sigma_1);
				for(int c=0; c<dim; c++) {
					sum[c] += G1 * s_sum[c];
					weight[c] += G1 * w_sum[c];
				}
			}

			for(int c=0; c<dim; c++) {
				lic.at<float>(y, x*dim+c) = (float)(1.0 - sum[c] / (ratio * weight[c]));
			}
		}

		// Show progress bar
		progress += width * dim;
		showProgress(progress, totalStep);			
	}
}

void pencilDrawing(cv::Mat& input, cv::Mat& out, std::vector<cv::Point2f>& points) {
	int width = input.cols;
	int height = input.rows;
	int dim = input.channels();

	cv::Mat img;
	if(input.depth() == CV_8U) {
		input.convertTo(img, CV_MAKETYPE(CV_32F, dim), 1.0 / 255.0);
	} else {
		input.convertTo(img, CV_MAKETYPE(CV_32F, dim));
	}

	// * Detect edge DoF
	cv::Mat gray, edge;
	cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
	npr::edgeDoG(gray, edge);

	// Compute ETF
	cv::Mat vfield;
	npr::calcVectorField(gray, vfield, ksize);

	// Quantize direction using distance field
	quantizeOrientation(vfield, edge);
	
	// * Generate noise
	cv::Mat noise;
	double ratio = 1.0;
	if(points.empty()) {
		int nNoise = (int)(0.2 * (double)(width * height));
		noise = cv::Mat(height, width, CV_32FC1);
		npr::uniformNoise(noise, gray, (int)(0.3 * width * height));
		ratio = 1.5 * nNoise / (width * height);
	} else {
		int nNoise = (int)points.size();
		noise = cv::Mat::zeros(height, width, CV_32FC1);
		for(int i=0; i<nNoise; i++) {
			int px = (int)points[i].x;
			int py = (int)points[i].y;
			cv::rectangle(noise, cv::Rect(px, py, 1, 1), cv::Scalar(1.0, 1.0, 1.0), -1);
			ratio = 1.2 * nNoise / (width * height);
		}
	}

	// * Convolution
	LIConv(out, img, vfield, noise, ratio);
}

void pencilDrawingLOD(cv::Mat& img, cv::Mat& out, std::vector<cv::Point2f>& points, int level) {
	int width = img.cols;
	int height = img.rows;
	int dim = img.channels();

	cv::Mat I, O;
	std::vector<cv::Point2f> P;
	out = cv::Mat::zeros(height, width, CV_MAKETYPE(CV_32F, dim));
	for(int l=level; l>=1; l--) {
		double scale = 1.0 / pow(2.0, l-1);
		
		cv::resize(img, I, cv::Size(), scale, scale, cv::INTER_CUBIC);

		P.clear();
		for(int i=0; i<points.size(); i++) {
			cv::Point2f q;
			q.x = (float)(points[i].x * scale);
			q.y = (float)(points[i].y * scale);
			P.push_back(q);
		}

		// * Pencil style rendering
		pencilDrawing(I, O, P);
		cv::resize(O, O, cv::Size(width, height), 0.0, 0.0, cv::INTER_CUBIC);
		out  = out + O;
	}
	out = out / (float)level;
}

