#include <opencv2/core/core.hpp>
#include <opencv2/core/types_c.h>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/flann/miniflann.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/nonfree/features2d.hpp>
#include <utilite/ULogger.h>
#include <utilite/UTimer.h>
#include <utilite/UConversion.h>
#include <utilite/UStl.h>
#include <utilite/UMath.h>
#include <fstream>
#include <vector>
#include <list>
#include <string>
#include <iostream>

void showUsage()
{
	printf("Usage:\n"
			"vocabularyComparison.exe \"dictionary/path\"\n"
			"  Dictionary path example: \"data/Dictionary49k.txt\""
			"  Note that 400 first descriptors in the file are used as queries.\n");
	exit(1);
}

int main(int argc, char * argv[])
{
	if(argc < 2)
	{
		showUsage();
	}
	ULogger::setType(ULogger::kTypeConsole);
	ULogger::setLevel(ULogger::kDebug);

	std::string dictionaryPath = argv[argc-1];
	std::list<std::vector<float> > objectDescriptors;
	//std::list<std::vector<float> > descriptors;
	std::map<int, std::vector<float> > descriptors;
	unsigned int dimension  = 0;
	UTimer timer;
	int objectDescriptorsSize= 400;

	std::ifstream file;
	if(!dictionaryPath.empty())
	{
		file.open(dictionaryPath.c_str(), std::ifstream::in);
	}
	if(file.good())
	{
		UDEBUG("Loading the dictionary from \"%s\"", dictionaryPath.c_str());

		// first line is the header
		std::string str;
		std::list<std::string> strList;
		std::getline(file, str);
		strList = uSplitNumChar(str);
		for(std::list<std::string>::iterator iter = strList.begin(); iter != strList.end(); ++iter)
		{
			if(uIsDigit(iter->at(0)))
			{
				dimension = std::atoi(iter->c_str());
				break;
			}
		}

		if(dimension == 0 || dimension > 1000)
		{
			UERROR("Invalid dictionary file, visual word dimension (%d) is not valid, \"%s\"", dimension, dictionaryPath.c_str());
		}
		else
		{
			int descriptorsLoaded = 0;
			// Process all words
			while(file.good())
			{
				std::getline(file, str);
				strList = uSplit(str);
				if(strList.size() == dimension+1)
				{
					//first one is the visual word id
					std::list<std::string>::iterator iter = strList.begin();
					int id = atoi(iter->c_str());
					++iter;

					std::vector<float> descriptor(dimension);
					unsigned int i=0;

					//get descriptor
					for(;i<dimension && iter != strList.end(); ++i, ++iter)
					{
						descriptor[i] = std::atof(iter->c_str());
					}
					if(i != dimension)
					{
						UERROR("");
					}

					if(++descriptorsLoaded<=objectDescriptorsSize)
					{
						objectDescriptors.push_back(descriptor);
					}
					else
					{
						//descriptors.push_back(descriptor);
						descriptors.insert(std::make_pair(id, descriptor));
					}
				}
				else if(str.size())
				{
					UWARN("Cannot parse line \"%s\"", str.c_str());
				}
			}
		}

		UDEBUG("Time loading dictionary = %fs, dimension=%d", timer.ticks(), dimension);
	}
	else
	{
		UERROR("Cannot open dictionary file \"%s\"", dictionaryPath.c_str());
	}
	file.close();

	if(descriptors.size() && objectDescriptors.size() && dimension)
	{
		cv::Mat dataTree;
		cv::Mat queries;

		UDEBUG("Creating data structures...");
		// Create the data structure
		dataTree = cv::Mat(descriptors.size(), dimension, CV_32F); // SURF descriptors are CV_32F
		{//scope
			//std::list<std::vector<float> >::const_iterator iter = descriptors.begin();
			std::map<int, std::vector<float> >::const_iterator iter = descriptors.begin();
			for(unsigned int i=0; i < descriptors.size(); ++i, ++iter)
			{
				UTimer tim;
				//memcpy(dataTree.ptr<float>(i), iter->data(), dimension*sizeof(float));
				memcpy(dataTree.ptr<float>(i), iter->second.data(), dimension*sizeof(float));
				//if(i%100==0)
				//	UDEBUG("i=%d/%d tim=%fs", i, descriptors.size(), tim.ticks());
			}
		}

		queries = cv::Mat(objectDescriptors.size(), dimension, CV_32F); // SURF descriptors are CV_32F
		{//scope
			std::list<std::vector<float> >::const_iterator iter = objectDescriptors.begin();
			for(unsigned int i=0; i < objectDescriptors.size(); ++i, ++iter)
			{
				UTimer tim;
				memcpy(queries.ptr<float>(i), iter->data(), dimension*sizeof(float));
				//if(i%100==0)
				//	UDEBUG("i=%d/%d tim=%fs", i, objectDescriptors.size(), tim.ticks());
			}
		}

		UDEBUG("descriptors.size()=%d, objectDescriptorsSize=%d, copying data = %f s",descriptors.size(), objectDescriptors.size(), timer.ticks());

		UDEBUG("Creating indexes...");
		cv::flann::Index * linearIndex = new cv::flann::Index(dataTree, cv::flann::LinearIndexParams());
		UDEBUG("Time to create linearIndex = %f s", timer.ticks());

		cv::flann::Index * kdTreeIndex1 = new cv::flann::Index(dataTree, cv::flann::KDTreeIndexParams(1));
		UDEBUG("Time to create kdTreeIndex1 = %f s", timer.ticks());

		cv::flann::Index * kdTreeIndex4 = new cv::flann::Index(dataTree, cv::flann::KDTreeIndexParams(4));
		UDEBUG("Time to create kdTreeIndex4 = %f s", timer.ticks());

		cv::flann::Index * kMeansIndex = new cv::flann::Index(dataTree, cv::flann::KMeansIndexParams());
		UDEBUG("Time to create kMeansIndex = %f s", timer.ticks());

		cv::flann::Index * compositeIndex = new cv::flann::Index(dataTree, cv::flann::CompositeIndexParams());
		UDEBUG("Time to create compositeIndex = %f s", timer.ticks());

		//cv::flann::Index * autoTunedIndex = new cv::flann::Index(dataTree, cv::flann::AutotunedIndexParams());
		//UDEBUG("Time to create autoTunedIndex = %f s", timer.ticks());


		UDEBUG("Search indexes...");
		int k=2; // 2 nearest neighbors
		cv::Mat results(queries.rows, k, CV_32SC1); // results index
		cv::Mat dists(queries.rows, k, CV_32FC1); // Distance results are CV_32FC1

		linearIndex->knnSearch(queries, results, dists, k);
		//std::cout << results.t() << std::endl;
		cv::Mat transposedLinear = dists.t();
		UDEBUG("Time to search linearIndex = %f s", timer.ticks());

		kdTreeIndex1->knnSearch(queries, results, dists, k);
		//std::cout << results.t() << std::endl;
		cv::Mat transposed = dists.t();
		UDEBUG("Time to search kdTreeIndex1 = %f s (size=%d, dist error(k1,k2)=(%f,%f))",
				timer.ticks(),
				transposed.cols,
				uMeanSquaredError(  (float*)transposed.data,
					transposed.cols,
					(float*)transposedLinear.data,
					transposedLinear.cols),
				uMeanSquaredError(  &transposed.at<float>(1,0),
					transposed.cols,
					&transposedLinear.at<float>(1,0),
					transposedLinear.cols));

		kdTreeIndex4->knnSearch(queries, results, dists, k);
		//std::cout << results.t() << std::endl;
		transposed = dists.t();
		UDEBUG("Time to search kdTreeIndex4 = %f s (size=%d, dist error(k1,k2)=(%f,%f))",
				timer.ticks(),
				transposed.cols,
				uMeanSquaredError(  (float*)transposed.data,
					transposed.cols,
					(float*)transposedLinear.data,
					transposedLinear.cols),
				uMeanSquaredError(  &transposed.at<float>(1,0),
					transposed.cols,
					&transposedLinear.at<float>(1,0),
					transposedLinear.cols));

		kMeansIndex->knnSearch(queries, results, dists, k);
		//std::cout << results.t() << std::endl;
		transposed = dists.t();
		UDEBUG("Time to search kMeansIndex = %f s (size=%d, dist error(k1,k2)=(%f,%f))",
				timer.ticks(),
				transposed.cols,
				uMeanSquaredError(  (float*)transposed.data,
					transposed.cols,
					(float*)transposedLinear.data,
					transposedLinear.cols),
				uMeanSquaredError(  &transposed.at<float>(1,0),
					transposed.cols,
					&transposedLinear.at<float>(1,0),
					transposedLinear.cols));

		compositeIndex->knnSearch(queries, results, dists, k);
		//std::cout << results.t() << std::endl;
		transposed = dists.t();
		UDEBUG("Time to search compositeIndex = %f s (size=%d, dist error(k1,k2)=(%f,%f))",
				timer.ticks(),
				transposed.cols,
				uMeanSquaredError(  (float*)transposed.data,
					transposed.cols,
					(float*)transposedLinear.data,
					transposedLinear.cols),
				uMeanSquaredError(  &transposed.at<float>(1,0),
					transposed.cols,
					&transposedLinear.at<float>(1,0),
					transposedLinear.cols));

		//autoTunedIndex->knnSearch(queries, results, dists, k);
		//UDEBUG("Time to search autoTunedIndex = %f s", timer.ticks());

		delete linearIndex;
		delete kdTreeIndex1;
		delete kdTreeIndex4;
		delete kMeansIndex;
		delete compositeIndex;
		//delete autoTunedIndex;
	}

    return 0;
}