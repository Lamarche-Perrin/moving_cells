/*
 * This file is part of Moving Cells.
 *
 * Moving Cells is is a digital installation building on a depth sensor to
 * allow spectators to interact with a cloud of particles through their movements.
 * It has been developed and first displayed in June 2015 by Robin Lamarche-Perrin
 * and Bruno Pace for the eponymous dance festival, in Leipzig.
 * See: http://www.movingcells.org
 * 
 * The current version of the program is implemented on Kinect for Windows v2 (K4W2)
 * through the open source driver libreenect2.
 * See: https://github.com/OpenKinect/libfreenect2
 * 
 * Copyright © 2015-2017 Robin Lamarche-Perrin and Bruno Pace
 * (<Robin.Lamarche-Perrin@lip6.fr>)
 * 
 * Moving Cells is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 * 
 * Moving Cells is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include <iostream>
#include <cstdio>
#include <sys/time.h>
#include <pthread.h>

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

int camId = 0;
unsigned int maxDelay = 60;
double switchingTime = 0;

const bool initVertical = false;
const bool initReverse = false;
const bool initSymmetric = false;
const bool useSymmetric = false;

const int frameWidth = 1280; // 640 (cam1)   1280 (cam2)   1024 (screen)
const int frameHeight = 720; // 360 (cam1)    720 (cam2)    768 (screen)

// 1280 x 720 // 1024 x 576 // 864 x 480 // 800 x 448 // 640 x 360 //

const bool cropFrame = false;
const int cropWidth = 159;
const int cropHeight = 0;

const bool resizeFrame = false;
const int windowWidth = 1024;
const int windowHeight = 768;

const bool parallelComputation = true;


const cv::Rect cropRectangle = cv::Rect (cropWidth, cropHeight, frameWidth - 2*cropWidth, frameHeight - 2*cropHeight);

bool stop = false;
bool vertical = initVertical;
bool reverse = initReverse;
bool symmetric = initSymmetric;
unsigned int delay = 1;

cv::VideoCapture cam;
cv::Mat *frameArray;
cv::Mat finalFrame;

int newDelay;
int displayDelay;

int currentDelay;
cv::Mat *currentFrame;
cv::Vec3b *currentPixel;

int workingDelay;
cv::Mat *workingFrame;
cv::Vec3b *workingPixel;

int rowSize, colSize;

void *status;
pthread_attr_t attr;
pthread_t frameThread;
pthread_t displayThread;
pthread_t computeThread;


void *computeVertical (void *arg);
void *computeVerticalSymmetric (void *arg);
void *computeVerticalReverse (void *arg);
void *computeVerticalReverseSymmetric (void *arg);
void *computeHorizontal (void *arg);
void *computeHorizontalSymmetric (void *arg);
void *computeHorizontalSymmetricBis (void *arg);
void *computeHorizontalReverse (void *arg);
void *computeHorizontalReverseSymmetric (void *arg);

void *displayFrame (void *arg);
void *getFrame (void *arg);


std::string type2str (int type) {
	std::string r;

	uchar depth = type & CV_MAT_DEPTH_MASK;
	uchar chans = 1 + (type >> CV_CN_SHIFT);

	switch (depth) {
    case CV_8U: r = "8U"; break;
    case CV_8S: r = "8S"; break;
    case CV_16U: r = "16U"; break;
    case CV_16S: r = "16S"; break;
    case CV_32S: r = "32S"; break;
    case CV_32F: r = "32F"; break;
    case CV_64F: r = "64F"; break;
    default: r = "User"; break;
	}

	r += "C";
	r += (chans+'0');

	return r;
}


int main (int argc, char *argv[])
{
	if (argc > 1) { camId = atoi(argv[1]); }
	if (argc > 2) { maxDelay = atoi(argv[2]); }
	if (argc > 3) { switchingTime = atof(argv[3]); }

	cam.open (camId);
 
	if (!cam.isOpened()) { std::cout << "cannot open camera"; }

	//cv::Mat blanck;
	//cam.read (blanck);
	cam.set (CV_CAP_PROP_FOURCC, CV_FOURCC('M','J','P','G'));
	cam.set (CV_CAP_PROP_FPS, 30);
	cam.set (CV_CAP_PROP_FRAME_WIDTH, frameWidth);
	cam.set (CV_CAP_PROP_FRAME_HEIGHT, frameHeight);
	//cam.read (blanck);

    double fps = cam.get (CV_CAP_PROP_FPS);
	double currentWidth = cam.get (CV_CAP_PROP_FRAME_WIDTH);
	double currentHeight = cam.get (CV_CAP_PROP_FRAME_HEIGHT);
	std::cout << "width: " << currentWidth << " pixels / height: " << currentHeight << " pixels / fps: " << fps << std::endl;
	
	double time;
	double subtime;
	struct timeval startTime, endTime;
	gettimeofday (&startTime, NULL);

	int frameNb = 0;
	int subframeNb = 0;

	delay = 1;
	newDelay = 0;
	frameArray = new cv::Mat [maxDelay+2];
	rowSize = ((float) frameHeight / (float) maxDelay);
	colSize = ((float) frameWidth / (float) maxDelay);
	std::cout << "cols: " << colSize << " pixels / rows: " << rowSize << " pixels" << std::endl;

	std::cout << "INIT: ";
	while (newDelay < maxDelay+1)
	{
		cam.read (frameArray[newDelay]);

		if (newDelay == 0)
		{
			std::string ty =  type2str (frameArray[newDelay].type());
			printf ("Matrix: %s %dx%d \n", ty.c_str(), frameArray[newDelay].cols, frameArray[newDelay].rows);
		}

		newDelay++;
		frameNb++;
		std::cout << frameNb << " ";
	}
	std::cout << std::endl;

	cv::namedWindow("webcam-delays", CV_WINDOW_NORMAL);
	cv::setWindowProperty ("webcam-delays", CV_WND_PROP_FULLSCREEN, 1);
	
		
	while (!stop)
	{
		gettimeofday (&endTime, NULL);
		double deltaTime = (endTime.tv_sec - startTime.tv_sec) + (float) (endTime.tv_usec - startTime.tv_usec) / 1000000L;
		startTime = endTime;

		time += deltaTime;
		subtime += deltaTime;

		frameNb++;
		subframeNb++;

		if (subtime >= 3)
		{
			std::cout << "CAM: " << (int) (((float) subframeNb) / subtime) << "fps" << std::endl;
			subtime = 0;
			subframeNb = 0;
		}

		if (newDelay >= maxDelay+2) { newDelay = 0; }
		
		displayDelay = newDelay+1 + (maxDelay - delay);
		while (displayDelay >= maxDelay+2) { displayDelay -= (maxDelay + 2); }

		currentDelay = displayDelay+1;
		if (currentDelay >= maxDelay+2) { currentDelay -= (maxDelay + 2); }

		finalFrame = frameArray[currentDelay].clone();
		//currentFrame = &frameArray[currentDelay];
		currentPixel = finalFrame.ptr<cv::Vec3b>(0);

		struct timeval start, end;
		gettimeofday (&start, NULL);
	
		if (parallelComputation)
		{
			int t2 = pthread_create (&frameThread, NULL, getFrame, NULL);
			if (t2) { std::cout << "Error: unable to create thread " << t2 << std::endl; exit(-1); }

			int t3;
			if (vertical) {
				if (reverse) {
					if (symmetric) { t3 = pthread_create (&computeThread, NULL, computeVerticalReverseSymmetric, NULL); }
					else { t3 = pthread_create (&computeThread, NULL, computeVerticalReverse, NULL); }
				} else {
					if (symmetric) { t3 = pthread_create (&computeThread, NULL, computeVerticalSymmetric, NULL); }
					else { t3 = pthread_create (&computeThread, NULL, computeVertical, NULL); }
				}
			} else {
				if (reverse) {
					if (symmetric) { t3 = pthread_create (&computeThread, NULL, computeHorizontalReverseSymmetric, NULL); }
					else { t3 = pthread_create (&computeThread, NULL, computeHorizontalReverse, NULL); }
				} else {
					if (symmetric) { t3 = pthread_create (&computeThread, NULL, computeHorizontalSymmetric, NULL); }
					else { t3 = pthread_create (&computeThread, NULL, computeHorizontal, NULL); }
				}
			}
			if (t3) { std::cout << "Error: unable to create thread " << t3 << std::endl; exit(-1); }

			t2 = pthread_join (frameThread, &status);
			if (t2) { std::cout << "Error: unable to join " << t2 << std::endl; exit(-1); }

			t3 = pthread_join (computeThread, &status);
			if (t3) { std::cout << "Error: unable to join " << t3 << std::endl; exit(-1); }

			
			int t1 = pthread_create (&displayThread, NULL, displayFrame, NULL);
			if (t1) { std::cout << "Error: unable to create thread " << t1 << std::endl; exit(-1); }

			t1 = pthread_join (displayThread, &status);
			if (t1) { std::cout << "Error: unable to join " << t1 << std::endl; exit(-1); }
		}

		else {
			displayFrame (0);
			getFrame (0);
			if (vertical) {
				if (reverse) { computeVerticalReverse(0); }
				else { computeVertical(0); }
			} else {
				if (reverse) { computeHorizontalReverse(0); }
				else { computeHorizontal(0); }
			}
		}

		gettimeofday (&end, NULL);
		double delta = (end.tv_sec - start.tv_sec) + (float) (end.tv_usec - start.tv_usec) / 1000000L;

		displayDelay = newDelay;
		newDelay++;
		

		if (switchingTime > 0 && time > switchingTime)
		{
			vertical = !vertical;
			if (useSymmetric && vertical) { symmetric = !symmetric; }
			if ((useSymmetric && vertical && symmetric) || (!useSymmetric && vertical)) { reverse = !reverse; }
			time = 0;
		}
	}
	
	return 0;
}

void *displayFrame (void *arg)
{
	struct timeval start, end;
	gettimeofday (&start, NULL);

	cv::flip (finalFrame, finalFrame, 1);
	if (cropFrame) { finalFrame = finalFrame(cropRectangle); }
	if (resizeFrame) { cv::resize (finalFrame, finalFrame, cv::Size (windowWidth, windowHeight)); }
	
	//cv::GaussianBlur (*currentFrame, *currentFrame, cv::Size(7,7), 1.5, 1.5);
	cv::imshow ("webcam-delays", finalFrame);

	int key = cv::waitKey(1);
	if (key > 0)
	{
		key = key & 0xFF;
		std::cout << "KEY PRESSED: " << key << std::endl;
					
		switch (key)
		{
		case 27 : // ESC escape
			stop = true;
			break;
			
		case 114 : // r
			reverse = !reverse;
			break;

		case 115 : // s
			symmetric = !symmetric;
			break;

		case 104 : // h
			vertical = !vertical;
			break;
			
		case 118 : // v
			vertical = !vertical;
			break;

		case 171 : // +
			delay++; if (delay > maxDelay) delay = maxDelay;
			std::cout << "DELAY = " << delay << std::endl;
			break;

		case 173 : // -
			delay--; if (delay <= 1) delay = 1;
			std::cout << "DELAY = " << delay << std::endl;
			break;

		case 141 : // Enter
			if (delay < maxDelay) delay = maxDelay;
			else delay = 1;
			std::cout << "DELAY = " << delay << std::endl;
			break;
		}
	}
	
	gettimeofday (&end, NULL);
	double delta = (end.tv_sec - start.tv_sec) + (float) (end.tv_usec - start.tv_usec) / 1000000L;

	if (parallelComputation) { pthread_exit (NULL); }
}


void *getFrame (void *arg)
{
	struct timeval start, end;
	gettimeofday (&start, NULL);

	cam.read (frameArray[newDelay]);
			
	gettimeofday (&end, NULL);
	double delta = (end.tv_sec - start.tv_sec) + (float) (end.tv_usec - start.tv_usec) / 1000000L;

	if (parallelComputation) { pthread_exit (NULL); }
}





void *computeVertical (void *arg)
{
	workingDelay = currentDelay;
	float firstCol = ((float) frameWidth / (float) delay);
	
	for (unsigned int d = 1; d < delay; d++)
	{
		workingDelay++;
		if (workingDelay >= maxDelay+2) { workingDelay = 0; }
		workingPixel = frameArray[workingDelay].ptr<cv::Vec3b>(0);

		float lastCol = (d+1) * ((float) frameWidth / (float) delay);

		for (unsigned int c = (int) firstCol; c < (int) lastCol; c++)
		{
			unsigned int i = c;
			for (int r = 0; r < frameHeight; r++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i += frameWidth;
			}
		}
		firstCol = lastCol;
	}

	if (parallelComputation) { pthread_exit (NULL); }
}


void *computeVerticalSymmetric (void *arg)
{
	workingDelay = currentDelay + delay/2;
	if (workingDelay >= maxDelay+2) { workingDelay -= (maxDelay+2); }	
	float firstCol = ((float) frameWidth / (float) delay);
	
	for (unsigned int d = 1; d < delay/2; d++)
	{
		workingDelay++;
		if (workingDelay >= maxDelay+2) { workingDelay = 0; }
		workingPixel = frameArray[workingDelay].ptr<cv::Vec3b>(0);

		float lastCol = (d+1) * ((float) frameWidth / (float) delay);

		for (unsigned int c = (int) firstCol; c < (int) lastCol; c++)
		{
			unsigned int i = c;
			for (int r = 0; r < frameHeight; r++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i += frameWidth;
			}

			i = (frameWidth-1) - c;
			for (int r = 0; r < frameHeight; r++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i += frameWidth;
			}

		}
		firstCol = lastCol;
	}

	if (parallelComputation) { pthread_exit (NULL); }
}


void *computeVerticalReverse (void *arg)
{
	workingDelay = currentDelay;
	float firstCol = (delay-1) * ((float) frameWidth / (float) delay);
	
	for (unsigned int d = 1; d < delay; d++)
	{
		workingDelay++;
		if (workingDelay >= maxDelay+2) { workingDelay = 0; }
		workingPixel = frameArray[workingDelay].ptr<cv::Vec3b>(0);

		float lastCol = (delay-(d+1)) * ((float) frameWidth / (float) delay);

		for (unsigned int c = (int) firstCol; c > (int) lastCol; c--)
		{
			unsigned int i = c;
			for (int r = 0; r < frameHeight; r++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i += frameWidth;
			}
		}
		firstCol = lastCol;
	}

	if (parallelComputation) { pthread_exit (NULL); }
}


void *computeVerticalReverseSymmetric (void *arg)
{
	workingDelay = currentDelay + delay/2;
	if (workingDelay >= maxDelay+2) { workingDelay -= (maxDelay+2); }	
	float firstCol = (delay-1) * ((float) frameWidth / (float) delay);
	
	for (unsigned int d = 1; d < delay/2; d++)
	{
		workingDelay++;
		if (workingDelay >= maxDelay+2) { workingDelay = 0; }
		workingPixel = frameArray[workingDelay].ptr<cv::Vec3b>(0);

		float lastCol = (delay/2-(d+1)) * ((float) frameWidth / (float) delay);

		for (unsigned int c = (int) firstCol; c > (int) lastCol; c--)
		{
			unsigned int i = c;
			for (int r = 0; r < frameHeight; r++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i += frameWidth;
			}

			i = frameWidth - c;
			for (int r = 0; r < frameHeight; r++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i += frameWidth;
			}
		}
		firstCol = lastCol;
	}

	if (parallelComputation) { pthread_exit (NULL); }
}




void *computeHorizontal (void *arg)
{
	workingDelay = currentDelay; // + (maxDelay - delay);
	//if (workingDelay >= maxDelay+2) { workingDelay -= (maxDelay+2); }
	float firstRow = ((float) frameHeight / (float) delay);
	
	for (unsigned int d = 1; d < delay; d++)
	{
		workingDelay++;
		if (workingDelay >= maxDelay+2) { workingDelay = 0; }
		workingPixel = frameArray[workingDelay].ptr<cv::Vec3b>(0);

		float lastRow = (d+1) * ((float) frameHeight / (float) delay);

		for (unsigned int r = (int) firstRow; r < (int) lastRow; r++)
		{
			unsigned int i = r * frameWidth;
			for (int c = 0; c < frameWidth; c++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i++;
			}
		}
		firstRow = lastRow;
	}

	if (parallelComputation) { pthread_exit (NULL); }
}


void *computeHorizontalSymmetric (void *arg)
{
	workingDelay = currentDelay + delay/2;
	if (workingDelay >= maxDelay+2) { workingDelay -= (maxDelay+2); }	
	float firstRow = ((float) frameHeight / (float) delay);
	
	for (unsigned int d = 1; d < delay/2; d++)
	{
		workingDelay++;
		if (workingDelay >= maxDelay+2) { workingDelay = 0; }
		workingPixel = frameArray[workingDelay].ptr<cv::Vec3b>(0);

		float lastRow = (d+1) * ((float) frameHeight / (float) delay);

		for (unsigned int r = (int) firstRow; r < (int) lastRow; r++)
		{
			unsigned int i = r * frameWidth;
			for (int c = 0; c < frameWidth; c++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i++;
			}

			i = (frameHeight - r) * frameWidth;
			for (int c = 0; c < frameWidth; c++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i++;
			}
		}
		firstRow = lastRow;
	}

	if (parallelComputation) { pthread_exit (NULL); }
}


void *computeHorizontalSymmetricBis (void *arg)
{
	workingDelay = currentDelay;
	float firstRow = ((float) frameHeight / (float) delay);
	
	for (unsigned int d = 1; d < delay; d++)
	{
		workingDelay++;
		if (workingDelay >= maxDelay+2) { workingDelay = 0; }
		workingPixel = frameArray[workingDelay].ptr<cv::Vec3b>(0);

		float lastRow = (d+1) * ((float) frameHeight / (float) delay);

		for (unsigned int r = (int) firstRow; r < (int) lastRow; r++)
		{
			unsigned int i = r * frameWidth;
			for (int c = 0; c < frameWidth/2; c++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i++;
			}
		}
		firstRow = lastRow;
	}

	workingDelay = currentDelay;
	firstRow = (delay-1) * ((float) frameHeight / (float) delay);
	
	for (unsigned int d = 1; d < delay; d++)
	{
		workingDelay++;
		if (workingDelay >= maxDelay+2) { workingDelay = 0; }
		workingPixel = frameArray[workingDelay].ptr<cv::Vec3b>(0);

		float lastRow = (delay-(d+1)) * ((float) frameHeight / (float) delay);

		for (unsigned int r = (int) firstRow; r > (int) lastRow; r--)
		{
			unsigned int i = r * frameWidth + frameWidth/2;
			for (int c = 0; c < frameWidth/2; c++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i++;
			}
		}
		firstRow = lastRow;
	}

	if (parallelComputation) { pthread_exit (NULL); }
}


void *computeHorizontalReverse (void *arg)
{
	workingDelay = currentDelay;
	float firstRow = (delay-1) * ((float) frameHeight / (float) delay);
	
	for (unsigned int d = 1; d < delay; d++)
	{
		workingDelay++;
		if (workingDelay >= maxDelay+2) { workingDelay = 0; }
		workingPixel = frameArray[workingDelay].ptr<cv::Vec3b>(0);

		float lastRow = (delay-(d+1)) * ((float) frameHeight / (float) delay);

		for (unsigned int r = (int) firstRow; r > (int) lastRow; r--)
		{
			unsigned int i = r * frameWidth;
			for (int c = 0; c < frameWidth; c++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i++;
			}
		}
		firstRow = lastRow;
	}

	if (parallelComputation) { pthread_exit (NULL); }
}


void *computeHorizontalReverseSymmetric (void *arg)
{
	workingDelay = currentDelay + delay/2;
	if (workingDelay >= maxDelay+2) { workingDelay -= (maxDelay+2); }	
	float firstRow = (delay-1) * ((float) frameHeight / (float) delay);
	
	for (unsigned int d = 1; d < delay; d++)
	{
		workingDelay++;
		if (workingDelay >= maxDelay+2) { workingDelay = 0; }
		workingPixel = frameArray[workingDelay].ptr<cv::Vec3b>(0);

		float lastRow = (delay/2-(d+1)) * ((float) frameHeight / (float) delay);

		for (unsigned int r = (int) firstRow; r > (int) lastRow; r--)
		{
			unsigned int i = r * frameWidth;
			for (int c = 0; c < frameWidth; c++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i++;
			}

			i = (frameHeight - r) * frameWidth;
			for (int c = 0; c < frameWidth; c++)
			{
				currentPixel[i][0] = workingPixel[i][0];
				currentPixel[i][1] = workingPixel[i][1];
				currentPixel[i][2] = workingPixel[i][2];
				i++;
			}
		}
		firstRow = lastRow;
	}

	if (parallelComputation) { pthread_exit (NULL); }
}

