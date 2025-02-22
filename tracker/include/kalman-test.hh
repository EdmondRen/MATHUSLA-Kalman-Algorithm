
#ifndef KD_DEFINE
#define KD_DEFINE

#include "physics.hh"
#include <vector>
#include <TMath.h>
#include <string>
#include "TrackFinder.hh"
#include "VertexFinder.hh"
#include <iostream>
#include <fstream>
#include <numeric>  
#include "kalman.hh"
#include "par_handler.hh"

class kalman_track
{
public:
  bool dropping;
  bool seed_was_used;
  bool finding;

  int status;

  //std::ofstream file;

  std::vector<int> king_move_inds;
  std::vector<double> x_scat;
  std::vector<double> z_scat;
  std::vector<std::vector<double>> x_s_list;
  std::vector<std::vector<double>> v_s_list;
  std::vector<double> x_s;
  //std::vector<double> P_s;
  std::vector<std::vector<double>> track_cov;
  //static double track_cov[7][7];
  std::vector<double> chi_s;
  std::vector<double> chi_f;
  Eigen::MatrixXd P_s0;


  std::vector<physics::digi_hit *> found_hits; // hits selected by find_good_hits()
  std::vector<physics::digi_hit *> added_hits;
  std::vector<physics::digi_hit *> unadded_hits;
  std::vector<physics::track *> added_tracks;
  std::vector<physics::track *> unadded_tracks;

  kalman_track(){};

  seed choose_seed(seed* current_seed);
  seed seed_in_use;

  void kalman_all(std::vector<physics::digi_hit *> trackhits, seed *current_seed);

  // void layer_sort(std::vector<physics::digi_hit *> digis)
  // {
  // //  std::ofstream file;
  //   //file.open("print.txt", std::ios_base::app);

  //   std::vector<std::vector<physics::digi_hit *>> layer_list(2 * detector::n_layers);

  //   std::vector<double> height_list(detector::n_layers);
  //   for (int i=0; i < detector::n_layers; i++) height_list[i] = (detector::LAYERS_Y[i][0] + detector::LAYERS_Y[i][1]) / 2;

  //   // make a map from y to the index of the layer
  //   std::map<double, int> y_to_inds;
  //   for (int i = 0; i < detector::n_layers; i++)
  //     y_to_inds[height_list[i]] = 2 * i; // just did this !!!!

  //   // sort floor AND wall hits by y interval
  //   for (auto hit : digis)
  //   {
  //     for (int i = 0; i < detector::n_layers; i++) {
  //       if (height_list[i] == hit->y) {
  //         layer_list[(int)(2 * i)].push_back(hit);
  //         break;
  //       }
  //       else if (height_list[i] < hit->y && hit->y < height_list[i+1]) {
  //         layer_list[(int)(2 * i + 1)].push_back(hit);
  //         break;
  //       }
  //     }
  //   }
  //   layer_hits = layer_list;

  //   // get indices of layers with hits
  //   // * layer: a list of layer indices that is non-empty
  //   // * det_ind_to_layers: indices of 'layer'
  //   for (int i = 0; i < layer_hits.size(); i++)
  //   {
  //     if (layer_hits[i].size() != 0)
  //       layers.push_back(i);
  //   }
  //   for (int i = 0; i < layers.size(); i++)
  //     det_ind_to_layers[layers[i]] = i;
  // }

  void layer_sort(std::vector<physics::digi_hit *> digis)
  {

    std::vector<std::vector<physics::digi_hit *>> layer_list(23);

    for (auto hit : digis)
    {   
      layer_list[hit->det_id.layerIndex].push_back(hit);

      // // Tracker layers
      // if (hit->det_id.layerIndex<detector::n_layers) {
      //   layer_list[(int) (hit->det_id.layerIndex)].push_back(hit);
      // }
      // // Wall layers (layerIndex=20,21)
      // else if (hit->det_id.layerIndex >=20){
      //   layer_list[hit->det_id.layerIndex-10].push_back(hit);
      // }
    }

    layer_hits = layer_list;

    // get indices of layers with hits
    for (int i = 0; i < layer_hits.size(); i++)
    {
      if (layer_hits[i].size() != 0)
        layers.push_back(i);
    }
    for (int i = 0; i < layers.size(); i++)
      det_ind_to_layers[layers[i]] = i;

  }  

  // Sort hits by time
  void time_sort(){

    std::vector<double> hits_time;
    std::vector<int> layers_sorted;
    for (int i = 0; i < layers.size(); i++){
      hits_time.push_back(layer_hits[layers[i]][0]->t);
    }

    // for (int i = 0; i < layers.size(); i++){
    //   std::cout<<  "layer: "<<  layers[i] << ", time: "<< hits_time[i]<< std::endl;
    // }    

    // Sort layer index with time
    std::vector<int> V(layers.size());
    std::iota(V.begin(),V.end(),0); //Initializing    
    std::sort( V.begin(),V.end(), [&](int i,int j) {return hits_time[i]<hits_time[j];} );

    for (int i = 0; i < V.size(); i++){
      // std::cout<<  "time sorted"<< V[i]<< std::endl;
      layers_sorted.push_back(layers[V[i]]);
    }
    layers = layers_sorted;

    // Remake det_ind_to_layers:
    for (int i = 0; i < layers.size(); i++)
    {
      det_ind_to_layers[layers[i]] = i; 
    }
  }

  void init_first_state();
  void init_matrices(seed *current_seed);
  void find_first();
  void filter();
  void smooth();

  KalmanFilter kf; // KalmanFilter object used for fitting
  KalmanFilter kf_find; // KalmanFilter object used for finding

  ParHandler* par_handler;

private:
  std::vector<int> layers;
  std::map<int, int> det_ind_to_layers;
  std::vector<std::vector<physics::digi_hit *>> layer_hits;

  std::vector<double> seedguess;

  int seed_layer; // layer (detector index) of first seed hit

  int n = 6; // Number of states x t z vx vy vz
  int m = 3; // Number of measurements x t z

  Eigen::MatrixXd A; // System dynamics matrix
  Eigen::MatrixXd C; // Output matrix
  Eigen::MatrixXd Q; // Process noise covariance
  Eigen::MatrixXd R; // Measurement noise covariance
  Eigen::MatrixXd P; // Estimate error covariance

  Eigen::VectorXd x0;

  physics::digi_hit *lowest_hit;
  physics::digi_hit *first_hit;
  std::vector<physics::digi_hit *> first_hit_list;

  std::vector<double> velocity;
  std::vector<int> added_layers;
  int filter_start_layer;

  bool skipped = false;

  Eigen::VectorXd find_guess(std::vector<double> seedguess, double dy)
  { // linearly project seedguess onto first layer and format for the filter
    //std::ofstream file;
    //file.open("print.txt", std::ios_base::app);

    Eigen::VectorXd y_guess(n);

    for (int i = 0; i < n; i++)
      y_guess[i] = seedguess[i];
    y_guess[1] = seedguess[6]; // y -> t

    double dt = -dy / seedguess[4];

    //file << " dt is " << dt << std::endl;

    y_guess[0] += dt * y_guess[3]; // x + dt * v_x
    y_guess[1] += dt;              // t + dt
    y_guess[2] += dt * y_guess[5]; // z + dt * v_z

    //file << " t is " << y_guess[1] << std::endl;

//    file.close();

    return y_guess;
  }

  void init_seed_info(seed *current_seed)
  {
    //std::ofstream file;
    //file.open("print.txt", std::ios_base::app);

    first_hit = current_seed->hits.first; // first hit in the seed
    first_hit_list = {first_hit};

    // layer of first hit (times two to account for spaces between layers)
    seed_layer = (first_hit->det_id).layerIndex;
    //file << "Seed layer is " << seed_layer << std::endl;

    seedguess = current_seed->guess();

    /*
    file << " seed guess is ";
    for (auto pos : seedguess)
      file << pos << ", ";
    file << std::endl;
    */
    x0 = Eigen::VectorXd::Zero(n);
    x0 << first_hit->x, first_hit->t, first_hit->z, seedguess[3], seedguess[4], seedguess[5];

   // file.close();
  }

  int find_next_layer()
  {
    //std::ofstream file;
    //file.open("print.txt", std::ios_base::app);

    int i = 0;
    int next_layer = -1;

    while (next_layer < seed_layer && i < layers.size())
    { // find highest layer on or below seed_layer_ind
      // with hits (or lowest above)
//      file << "next index is " << i << std::endl;
//      file << "next layer is " << next_layer << std::endl;

      if (layers[i] <= seed_layer)
        next_layer = layers[i];
      else
        break;
      i++;
    }
    if (next_layer == -1)
      next_layer = layers[0]; // lowest layer is above seed_layer

    //file.close();

    return next_layer;
  }

  void prepare_output()
  {
    //std::ofstream file;
    //file.open("print.txt", std::ios_base::app);

    if (!finding) {
      // prepare best estimate positions for visualiser
      for (int i = 0; i < kf.x_s.size(); i++) {
        x_s_list.push_back({kf.x_s[i][0], kf.added_hits[i]->y, kf.x_s[i][2]});
        v_s_list.push_back({kf.x_s[i][3], kf.x_s[i][4], kf.x_s[i][5]});
      }
      // prepare track state vector from first smoothed state
      x_s = {kf.x_s[0][0], layer_hits[layers[0]][0]->y, kf.x_s[0][2], kf.x_s[0][3], kf.x_s[0][4], kf.x_s[0][5], kf.x_s[0][1]};
      //x_s = {kf.x_s.back()[0], layer_hits[layers.back()][0]->y, kf.x_s.back()[2], kf.x_s.back()[3], kf.x_s.back()[4], kf.x_s.back()[5], kf.x_s.back()[1]}; //// PASSING LAST COVARIANCE TEMPORARILY

      // prepare track error vector from covariance matrix
      /*
      for (int i = 0; i < n; i++)
        P_s.push_back(std::sqrt(std::abs(kf.P_s[0](i, i))));
      P_s.push_back(P_s[1]);
      P_s[1] = layer_hits[layers[0]][0]->ey;
      */
      std::vector<std::vector<double>> _track_cov;
      Eigen::MatrixXd TC = kf.P_s[0];
      //Eigen::MatrixXd TC = kf.P_s.back(); //// PASSING LAST COVARIANCE TEMPORARILY
      _track_cov = {{TC(0,0),0,TC(0,2),TC(0,3),TC(0,4),TC(0,5),TC(0,1)},
                   {0      ,0,0      ,0      ,0      ,0      ,0      },
                   {TC(2,0),0,TC(2,2),TC(2,3),TC(2,4),TC(2,5),TC(2,1)},
                   {TC(3,0),0,TC(3,2),TC(3,3),TC(3,4),TC(3,5),TC(3,1)},
                   {TC(4,0),0,TC(4,2),TC(4,3),TC(4,4),TC(4,5),TC(4,1)},
                   {TC(5,0),0,TC(5,2),TC(5,3),TC(5,4),TC(5,5),TC(5,1)},
                   {TC(1,0),0,TC(1,2),TC(1,3),TC(1,4),TC(1,5),TC(1,1)}};

      _track_cov[1][1] = std::pow(layer_hits[layers[0]][0]->ey, 2);
      //_track_cov[1][1] = std::pow(layer_hits[layers.back()][0]->ey, 2); //// PASSING LAST COVARIANCE TEMPORARILY
      track_cov = _track_cov;
      /*
      for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 7; j++) {
          track_cov[i][j] = _track_cov[i][j];
        }
      }
      */
      //pass last covariance matrix
      P_s0 = kf.P_s[0];
      //P_s0 = kf.P_s.back(); //// PASSING LAST COVARIANCE TEMPORARILY
    }

    king_move_inds = kf.king_move_inds;

    //if (king_move_inds.size() != 0) std::cout << "\n k-test ";
    //for (auto ind : king_move_inds) std::cout << ind << ", ";

    added_hits = kf.added_hits;
    unadded_hits = kf.unadded_hits;

			// std::cout<<"kf.unadded (round1)"<<std::endl;
			// for (auto hit: unadded_hits) std::cout<<hit->index<<std::endl;    
			// std::cout<<"kf_find.unadded (round1)"<<std::endl;
			// for (auto hit: kf_find.unadded_hits) std::cout<<hit->index<<std::endl;          
			// std::cout<<"kf_find_bottomlayer.unadded (round1)"<<std::endl;


    // Dropping round 2 (hits that are dropped during backward finding)
    if (finding) unadded_hits.insert(unadded_hits.end(), kf_find.unadded_hits.begin(), kf_find.unadded_hits.end());

		if (par_handler->par_map["debug"] == 1){ 		
			std::cout<<"Dropped (backward finding)"<<std::endl;
			for (auto hit: kf_find.unadded_hits) std::cout<<hit->index<<std::endl;
		}    

    // Dropping round 3 (other hits that are on the seed layer)
    for (auto hit : layer_hits[filter_start_layer])
    {
      if (hit->index != lowest_hit->index)
      {
        unadded_hits.push_back(hit);
        // std::cout<<hit->index<<std::endl;        
      }
    }

//    file.close();
  }
};

class kalman_vertex
{
public:
  int status;
  bool dropping;

  std::vector<physics::track *> added_tracks;
  std::vector<physics::track *> unadded_tracks;

  //Eigen::VectorXd x_s;              // final vertex estimate
  std::vector<double> x_s;
  std::vector<double> pulls_v;

  ParHandler* par_handler;

  double chi_v;

  kalman_vertex(){};

  void vertexer(std::vector<physics::track *> tracks_list, vertex_seed *seed);

private:
  int s = 4; // Number of states x y z t
  int m = 6; // Number of measurements x t z vx vy vz
  int v = 3; // Number of velocities vx vy vz

  std::ofstream file;

  Eigen::MatrixXd A;              // (Tracker) system dynamics matrix
  Eigen::MatrixXd C;              // x Output matrix
  Eigen::MatrixXd P;              // x error covariance
  Eigen::MatrixXd Q;              // Process noise covariance
  //std::vector<Eigen::MatrixXd> B; // q Output matrix
  //std::vector<Eigen::MatrixXd> R; // Measurement noise (track) covariance
  //std::vector<Eigen::MatrixXd> D; // q error covariance
  //std::vector<Eigen::MatrixXd> E; // covariance {x,q}
  Eigen::MatrixXd B; // q Output matrix
  Eigen::MatrixXd R; // Measurement noise (track) covariance
  Eigen::MatrixXd D; // q error covariance
  Eigen::MatrixXd E; // covariance {x,q}

  Eigen::VectorXd x0;              // first (seed) vertex estimate
  //std::vector<Eigen::VectorXd> q0; // first (seed) velocity estimates
  Eigen::VectorXd q0; // first (seed) velocity estimates
  Eigen::VectorXd q1; // second (seed) velocity estimates

  KalmanFilter kfv;

  void init_matrices(vertex_seed *seed);

  void init_seed_info(vertex_seed *seed)
  {

    x0 = seed->guess_k();
/*
    physics::track * tr1 = seed->tracks.first;
    double dt = x0[3] - tr1->t0;

    x0 << tr1->x0 + dt * tr1->vx,
          tr1->y0 + dt * tr1->vy,
          tr1->z0 + dt * tr1->vz,
          tr1->t0 + dt;
*/
    /*
    Eigen::VectorXd first(3);

    first << seed->tracks.first->vx, seed->tracks.first->vy, seed->tracks.first->vz;

    q0 = {first, second};
    */
    Eigen::VectorXd first(3);
//    Eigen::VectorXd second(3);

    first << seed->tracks.first->vx, seed->tracks.first->vy, seed->tracks.first->vz;
//    second << seed->tracks.second->vx, seed->tracks.second->vy, seed->tracks.second->vz;

    q0 = first;
//    q1 = second;
  }

  void filter(std::vector<physics::track *> tracks_list);
  void smooth();

  void prepare_output()
  {
    for (int i=0; i < kfv.x_n.size(); i++) x_s.push_back(kfv.x_n[i]);

    pulls_v = kfv.pulls_v_s;
  }

};

#endif

