#ifdef USE_OPENCV
#include <opencv2/core/core.hpp>

#include <fstream>  // NOLINT(readability/streams)
#include <iostream>  // NOLINT(readability/streams)
#include <string>
#include <utility>
#include <vector>

#include "caffe/data_transformer.hpp"
#include "caffe/layers/base_data_layer.hpp"
#include "caffe/layers/image_data_layer.hpp"
#include "caffe/util/benchmark.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/util/rng.hpp"

#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/contrib/contrib.hpp>


namespace caffe {

void returnImageList(string ImagePath, vector<string>& fileNames)
{
	cv::Directory dir;
	fileNames = dir.GetListFiles(ImagePath, "*", false);
}

string fileparts(string filename)
{
    int idx0 = filename.find_first_of("/");
    string a = filename.substr(idx0+1,filename.length()-1);
    return a;
}

template <typename Dtype>
ImageDataLayer<Dtype>::~ImageDataLayer<Dtype>() {
  this->StopInternalThread();
}

template <typename Dtype>
void ImageDataLayer<Dtype>::DataLayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const int new_height = this->layer_param_.image_data_param().new_height();
  const int new_width  = this->layer_param_.image_data_param().new_width();
  const bool is_color  = this->layer_param_.image_data_param().is_color();
  string root_folder = this->layer_param_.image_data_param().root_folder();
  const int batch_size = this->layer_param_.image_data_param().batch_size();

  CHECK((new_height == 0 && new_width == 0) ||
      (new_height > 0 && new_width > 0)) << "Current implementation requires "
      "new_height and new_width to be set at the same time.";
  // Read the file with filenames and labels
  const string& source = this->layer_param_.image_data_param().source();
  LOG(INFO) << "Opening file " << source;
  std::ifstream infile(source.c_str());
  string filename;
  int label;
  while (infile >> filename >> label) {
    lines_.push_back(std::make_pair(filename, label));
  }

  if (this->layer_param_.image_data_param().shuffle()) {
    // randomly shuffle data
    LOG(INFO) << "Shuffling data";
    const unsigned int prefetch_rng_seed = caffe_rng_rand();
    prefetch_rng_.reset(new Caffe::RNG(prefetch_rng_seed));
    ShuffleImages();
  }
  LOG(INFO) << "A total of " << lines_.size() << " images.";

  lines_id_ = 0;
  // Check if we would need to randomly skip a few data points
  if (this->layer_param_.image_data_param().rand_skip()) {
    unsigned int skip = caffe_rng_rand() %
        this->layer_param_.image_data_param().rand_skip();
    LOG(INFO) << "Skipping first " << skip << " data points.";
    CHECK_GT(lines_.size(), skip) << "Not enough points to skip";
    lines_id_ = skip;
  }

  string imagePath=root_folder + "/" + lines_[lines_id_].first;
  vector<string> fileNames;
  if (this->output_clip_markers_){
     imagePath=root_folder + "/" + fileparts(lines_[lines_id_].first);     
     returnImageList(imagePath, fileNames);
     imagePath=imagePath + "/"+ fileNames[0];

     vector<int> clipmarkers_shape(1, batch_size);
     top[2]->Reshape(clipmarkers_shape);
     for (int i = 0; i < this->PREFETCH_COUNT; ++i) {
       this->prefetch_[i].clip_markers_.Reshape(clipmarkers_shape);
     }
  }
  // Read an image, and use it to initialize the top blob.
  cv::Mat cv_img = ReadImageToCVMat(imagePath,
                                    new_height, new_width, is_color);
  CHECK(cv_img.data) << "Could not load " << imagePath;
  // Use data_transformer to infer the expected blob shape from a cv_image.
  vector<int> top_shape = this->data_transformer_->InferBlobShape(cv_img);
  this->transformed_data_.Reshape(top_shape);
  // Reshape prefetch_data and top[0] according to the batch_size.  
  CHECK_GT(batch_size, 0) << "Positive batch size required";
  top_shape[0] = batch_size;
  for (int i = 0; i < this->PREFETCH_COUNT; ++i) {
    this->prefetch_[i].data_.Reshape(top_shape);
  }
  top[0]->Reshape(top_shape);

  LOG(INFO) << "output data size: " << top[0]->num() << ","
      << top[0]->channels() << "," << top[0]->height() << ","
      << top[0]->width();
  // label
  vector<int> label_shape(1, batch_size);
  top[1]->Reshape(label_shape);
  for (int i = 0; i < this->PREFETCH_COUNT; ++i) {
    this->prefetch_[i].label_.Reshape(label_shape);
  }
}

template <typename Dtype>
void ImageDataLayer<Dtype>::ShuffleImages() {
  caffe::rng_t* prefetch_rng =
      static_cast<caffe::rng_t*>(prefetch_rng_->generator());
  shuffle(lines_.begin(), lines_.end(), prefetch_rng);
}

// This function is called on prefetch thread
template <typename Dtype>
void ImageDataLayer<Dtype>::load_batch(Batch<Dtype>* batch) {
	CPUTimer batch_timer;
	batch_timer.Start();
	double read_time = 0;
	double trans_time = 0;
	CPUTimer timer;
	CHECK(batch->data_.count());
	CHECK(this->transformed_data_.count());
	ImageDataParameter image_data_param = this->layer_param_.image_data_param();
	const int batch_size = image_data_param.batch_size();
	const int new_height = image_data_param.new_height();
	const int new_width = image_data_param.new_width();
	const bool is_color = image_data_param.is_color();
	string root_folder = image_data_param.root_folder();
        

	// Reshape according to the first image of each batch
	// on single input batches allows for inputs of varying dimension.
	string imagePath=root_folder + "/"+ lines_[lines_id_].first;
	vector<string> fileNames;
	int tbuffer;
	if (this->output_clip_markers_){
		tbuffer = batch_size / 16;
		imagePath=root_folder + "/" + fileparts(lines_[lines_id_].first);   
		returnImageList(imagePath, fileNames);
		imagePath=imagePath + "/"+fileNames[0];
	}
	// Read an image, and use it to initialize the top blob.
	cv::Mat cv_img = ReadImageToCVMat(imagePath,
			new_height, new_width, is_color);
	CHECK(cv_img.data) << "Could not load " << imagePath;
	// Use data_transformer to infer the expected blob shape from a cv_img.
	vector<int> top_shape = this->data_transformer_->InferBlobShape(cv_img);
	this->transformed_data_.Reshape(top_shape);
	// Reshape batch according to the batch_size.
	top_shape[0] = batch_size;
	batch->data_.Reshape(top_shape);

	Dtype* prefetch_data = batch->data_.mutable_cpu_data();
	Dtype* prefetch_label = batch->label_.mutable_cpu_data();

	// datum scales
	const int lines_size = lines_.size();
	if (this->output_clip_markers_){
		Dtype* prefetch_clip_markers = batch->clip_markers_.mutable_cpu_data();
		for (int item_id = 0; item_id < tbuffer; ++item_id) {
			// get a blob
			CHECK_GT(lines_size, lines_id_);
			imagePath=root_folder + "/" + fileparts(lines_[lines_id_].first);   
			returnImageList(imagePath, fileNames);

			int randID = (rand() % (fileNames.size()-16+1)); 
                        string imagePath1;
			for(int image_id=randID;image_id<16+randID;++image_id){
				imagePath1=imagePath+ "/"+fileNames[image_id];
				cv::Mat cv_img = ReadImageToCVMat(imagePath1,
						new_height, new_width, is_color);
				CHECK(cv_img.data) << "Could not load " << imagePath1;
				//LOG(INFO) << "ImagePath1" << imagePath1<<new_height<<new_width;
				/* suppose tbuffer=4
				 *  /  0  1  2  3   4   5   6  ... 16
				 * 0/  0  4  8  12  16  20  24 ... 64
				 * 1/  1  5  9  13  17  21  25 ... 65
				 * 2/  2  6  10 14  18  22  26 ... 66
				 * 3/  3  7  11 15  19  23  27 ... 67
				 * 				 */
				int imgPosition = tbuffer*(image_id-randID)+item_id;
				int offset = batch->data_.offset(imgPosition);
				// Apply transformations (mirror, crop...) to the image
				this->transformed_data_.set_cpu_data(prefetch_data + offset);
				//int rid=item_id*16+(image_id-randID);
				prefetch_label[imgPosition] = lines_[lines_id_].second;
				if(image_id==randID){
					this->data_transformer_->Transform(cv_img, &(this->transformed_data_),true);
					prefetch_clip_markers[imgPosition] = 0;
				}else{
					this->data_transformer_->Transform(cv_img, &(this->transformed_data_),false);
					prefetch_clip_markers[imgPosition] = 1;}				
			}
			// go to the next iter
			lines_id_++;
			if (lines_id_ >= lines_size) {
				// We have reached the end. Restart from the first.
				DLOG(INFO) << "Restarting data prefetching from start.";
				lines_id_ = 0;
			}
		}
	}else{
		for (int item_id = 0; item_id < batch_size; ++item_id) {
			// get a blob
			timer.Start();
			CHECK_GT(lines_size, lines_id_);
			cv::Mat cv_img = ReadImageToCVMat(root_folder + lines_[lines_id_].first,
					new_height, new_width, is_color);
			CHECK(cv_img.data) << "Could not load " << lines_[lines_id_].first;
			read_time += timer.MicroSeconds();
			timer.Start();
			// Apply transformations (mirror, crop...) to the image
			int offset = batch->data_.offset(item_id);
			this->transformed_data_.set_cpu_data(prefetch_data + offset);
			this->data_transformer_->Transform(cv_img, &(this->transformed_data_));
			trans_time += timer.MicroSeconds();

			prefetch_label[item_id] = lines_[lines_id_].second;
			// go to the next iter
			lines_id_++;
			if (lines_id_ >= lines_size) {
				// We have reached the end. Restart from the first.
				DLOG(INFO) << "Restarting data prefetching from start.";
				lines_id_ = 0;
				if (this->layer_param_.image_data_param().shuffle()) {
					ShuffleImages();
				}
			}
		}
		batch_timer.Stop();
		DLOG(INFO) << "Prefetch batch: " << batch_timer.MilliSeconds() << " ms.";
		DLOG(INFO) << "     Read time: " << read_time / 1000 << " ms.";
		DLOG(INFO) << "Transform time: " << trans_time / 1000 << " ms.";
	}
}
INSTANTIATE_CLASS(ImageDataLayer);
REGISTER_LAYER_CLASS(ImageData);

}  // namespace caffe
#endif  // USE_OPENCV
