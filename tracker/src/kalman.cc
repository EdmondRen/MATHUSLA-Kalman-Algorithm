/**
* Implementation of KalmanFilter. Based on code by
*
* Hayk Martirosyan (2014.11.15)
*
* and the following paper
*
* Frühwirth, R. (1987). Application of Kalman filtering to track and vertex fitting.
* Nuclear Instruments and Methods in Physics Research Section A:
* Accelerators, Spectrometers, Detectors and Associated Equipment,
* 262(2-3), 444-450. doi:10.1016/0168-9002(87)90887-4
*
* @authors: Keegan Humphrey and Hao Liao
* @date: May 2021
*/

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <Eigen/Dense>
#include "kalman.hh"
#include <unsupported/Eigen/MatrixFunctions>
#include "globals.hh"
#include "Geometry.hh"
#include "statistics.hh"
//#include "ROOT/Math.h"
#include "Math/ProbFunc.h"

KalmanFilter::KalmanFilter(
    double dy,
    const Eigen::MatrixXd &A,
    const Eigen::MatrixXd &C,
    const Eigen::MatrixXd &Q,
    const Eigen::MatrixXd &R,
    const Eigen::MatrixXd &P)
    : A(A), C(C), Q(Q), R(R), P0(P), f_matrix(A), P_add(A),
      m(C.rows()), n(A.rows()), dy(dy), initialized(false),
      I(n, n), x_hat(n), x_hat_new(n)
{
  I.setIdentity();
}

KalmanFilter::KalmanFilter() {}

void KalmanFilter::init_gain(const Eigen::VectorXd &x0, std::vector<physics::digi_hit *> first_layer)
{ // init function for the filter in the Gain Matrix Formalism

  // no propagation to first hit
//  dy = 0;
  A = I;

  P = P0;

  K = P * C.transpose() * (C * P * C.transpose() + R).inverse();

  chi = 0;

  // y value of hits on first layer
  y_val = first_layer[0]->y;
  y_val_err = first_layer[0]->ey;

  // use errors from first hit since they are the same for the entire layer
  R << first_layer[0]->ex, 0, 0,
      0, first_layer[0]->et, 0,
      0, 0, first_layer[0]->ez;
//  R << first_layer[0]->ex, 0, 0,
//      0, first_layer[0]->et * std::sqrt(2), 0,
//      0, 0, first_layer[0]->ez; // INCREASING T ERROR ARTIFICIALLY
  R = R * R;

  // pass seed predicted position and first vector
  // of layer_hits (hits in the layer) and then use find_nearest
  // to find the index of the best hit to use
  std::vector<int> x_inds = find_nearest(first_layer, x0);
  int x_ind = x_inds[0];

  // if none make the 1e6 cut, choose a random hit to start the filter
  if (x_ind == -1)
    x_ind = rand() % first_layer.size();

  // use position of closest hit for first state
  physics::digi_hit *y0 = first_layer[x_ind];
  x_hat << y0->x, y0->t, y0->z, x0[3], x0[4], x0[5];

  if (par_handler->par_map["debug"] == 1) {

    Eigen::VectorXd Y(m);
    Y << y0->x, y0->t, y0->z;
    Eigen::VectorXd v(3);
    v << x_hat[3], x_hat[4], x_hat[5];

    std::cout << "Filtering: First velocity is " << v.transpose() / constants::c <<
	" at y = " << y_val << " digi is " << Y.transpose() << std::endl;
  }

  initialized = true;

  // Storage for update() data
  x_p = {};          // Prediction State
  x_f = {x_hat};     // Filter State
  P_p = {};          // Prediction Cov
  P_f = {P};         // Filter Cov
  A_k = {};          // Action Matrix
  added_hits = {y0}; // List of used hits
}

double KalmanFilter::update_gain(const std::vector<physics::digi_hit *> y, double dy)
{

  // this->dy = dy;

  double chi = update_gain(y);
  return chi;
}

physics::digi_hit* KalmanFilter::merge_enery_sharing(const std::vector<physics::digi_hit *> &y_list, const std::vector<int> &hit_inds)
{
  if(hit_inds[0]==-1) {
    merged_hit = new physics::digi_hit();
    return merged_hit;}
  else if (hit_inds[1]==-1) return y_list[hit_inds[0]];
  else {
    auto hit0 = y_list[hit_inds[0]];
    auto hit1 = y_list[hit_inds[1]];
    auto long_direction_index = hit0->long_direction_index;

    // Get the distance between two events
    double dist_short,dist_long,dist_time;
    if (long_direction_index == 0) // if even layer, bars perpendicualr to beamline (z), z has better resolution
    {
      dist_short = std::abs(hit0->z - hit1->z);
      dist_long = std::abs(hit0->x - hit1->x);
      dist_time = std::abs(hit0->t - hit1->t);
    }
    else // if odd  layer, bars perpendicualr to x, x has better resolution
    {
      dist_short = std::abs(hit0->x - hit1->x);
      dist_long = std::abs(hit0->z - hit1->z);
      dist_time = std::abs(hit0->t - hit1->t);
    }  

    // if they are 1) from adjacent bars, and 2) with delta_time<2sigma, 3) delta_long_dist<2 sigma, merge the hits
    int nsigma=2;
    if ((dist_short<=detector::scintillator_width*1.1) && dist_long<(detector::time_resolution*(constants::c/constants::optic_fiber_n)/sqrt(2)*nsigma) && dist_time<(detector::time_resolution*nsigma) )  
    {
      merged_hit = hit0;

      // Make hit1 the average of (hit0, hit1)
      merged_hit->x = 0.5*(hit0->x+hit1->x);
      merged_hit->y = 0.5*(hit0->y+hit1->y);
      merged_hit->z = 0.5*(hit0->z+hit1->z);
      merged_hit->t = 0.5*(hit0->t+hit1->t);
      merged_hit->ex = merged_hit->ex /sqrt(2);
      merged_hit->ey = merged_hit->ey /sqrt(2);
      merged_hit->ez = merged_hit->ez /sqrt(2);
      merged_hit->et = merged_hit->et /sqrt(2);
      SECOND_HIT_SKIPPED=true;

      // erase hit1
      // y_list.erase(y_list.begin()+hit_inds[1]);
      // if (hit_inds[1]<hit_inds[0]){
      //   hit_inds[0]-=1;
      // }
    }

    return y_list[hit_inds[0]];

  }
}

double KalmanFilter::update_gain(const std::vector<physics::digi_hit *> y_list)
{ // predict and filter using the digi hits in the layer for tracker

  if (!initialized)
    throw std::runtime_error("Filter is not initialized!");

  // indices of lowest two chis in the layer
  std::vector<int> hit_inds = find_nearest(y_list, x_hat);
  // // if there is energy sharing, merge the hit with lowest chi2 with the one in the adjacent bar

  // Select the hit with lowest chi2. hit_inds[0] is index of hit w lowest chi that meets beta cut
  physics::digi_hit *y;
  y = y_list[hit_inds[0]];

  // Merge the adjacent hits with energy sharing
  // y = merge_enery_sharing(y_list, hit_inds);

  // remove adjacent hits
  // ** This function was actually turned off, but kept here for its other purpose of logging unused hits.
  king_moves_algorithm(y_list, hit_inds);

  // end if no good hit was found
  if (hit_inds[0] == -1)
    return -1.0;  


  // update matrices, using the hit with minimum chi2
  update_matrices(y);

  //x_hat_new = A * x_hat;
  x_hat_new = f_matrix * x_hat; // In EKF, we should use the non-linear function to predict the state, and use the Jacobian A to predict the covariance.

  P = A * P * A.transpose() + Q + P_add;
  K = P * C.transpose() * (C * P * C.transpose() + R).inverse();

  A_k.push_back(A); // F_k

  Eigen::VectorXd Y(m);
  Y << y->x, y->t, y->z;

  // hit is chosen, update y_val to hit->y
  y_val = y->y;
  y_val_err = y->ey;

  // calculate the increment in the chi squared
  Eigen::MatrixXd err_metric_p = R + C * P * C.transpose();
  double chi_plus = (Y - C * x_hat_new).transpose() * err_metric_p.inverse() * (Y - C * x_hat_new);

  added_inds.push_back(hit_inds[0]);
  added_hits.push_back(y);

  // calculate probability from the chi increment
  //Stat_Funcs sts;
  //double ndof = added_hits.size();
  //ndof = ndof > 1.0 ? 4.0 * ndof - 6.0 : 1.0;
  //chi_plus = sts.chi_prob(chi_plus, ndof);

  // store prediction data
  x_p.push_back(x_hat_new); // store for x^(k-1)_k
  P_p.push_back(P);         // C^(k-1)_k

  x_hat_new += K * (Y - C * x_hat_new);

  P = (I - K * C) * P;

  if (par_handler->par_map["debug"] == 1) {

    Eigen::VectorXd v(3);
    v << x_hat_new[3], x_hat_new[4], x_hat_new[5];

    std::cout << "Filtering: Residue is " << (Y - C * x_hat_new).transpose() << " at y = " << y_val << " with chi " << chi_plus <<
	" updated velocity is " << v.transpose() / constants::c << " digi is " << Y.transpose() << std::endl;
  }

  Eigen::MatrixXd err_metric_f = R - C * P * C.transpose();

  // stored filtered data
  P_f.push_back(P);         // C^k_k
  x_f.push_back(x_hat_new); // x^k_k

  x_hat = x_hat_new;

  // increment chi squared
  chi += chi_plus;

  return chi_plus;
}

void KalmanFilter::king_moves_algorithm(const std::vector<physics::digi_hit *> y_list, std::vector<int> hit_inds)
{
  for (int i = 0; i < y_list.size(); i++)
  { // remove hits that have a small residual from the chosen hit
    // this helps avoid making duplicate tracks

    // index of hit with min chi (chosen hit)
    if (i == hit_inds[0])
      continue;

    else if (i == hit_inds[1])
//    if (i == hit_inds[1])
    {
      if (SECOND_HIT_SKIPPED){
        SECOND_HIT_SKIPPED=false;
        continue;
      }
      update_matrices(y_list[i]);
      x_hat_new = f_matrix * x_hat;

      // residue for the hit
      Eigen::VectorXd res(3);
      res << y_list[i]->x, y_list[i]->t, y_list[i]->z;
      res = res - C * x_hat_new;

      std::vector<double> scale = {1, 2.5, 1};

      if (std::abs(res[0]) < std::sqrt(12) * y_list[i]->ex * scale[0])
      {
        if (std::abs(res[1]) < y_list[i]->et * scale[1])
        {
          if (std::abs(res[2]) < std::sqrt(12) * y_list[i]->ez * scale[2])
          {
//            king_move_inds.push_back(y_list[i]->index);

            // within king moves of the chosen hit, remove from hit pool
//            continue;
            unadded_hits.push_back(y_list[i]); // This should turn king_moves_algorithm off altogether
          }
        }
      }
      else
        unadded_hits.push_back(y_list[i]);
    }
    else
      unadded_hits.push_back(y_list[i]);
  }

}

void KalmanFilter::init_smooth_gain()
{ // init function for the smoother

  //starting smoothing process with the very last updated or filtered state/cov
  x_s = {x_hat};
  P_s = {P};

  chi = 0;

  if (par_handler->par_map["debug"] == 1) std::cout << std::endl;
};

double KalmanFilter::smooth_gain(const physics::digi_hit *y, int k)
{ // smooth the filtered states (propagate information from later hits to earlier ones)

  // smoother gain matrix(A_k in Fruhwirth paper)
  Eigen::MatrixXd S = P_f[k] * A_k[k].transpose() * P_p[k].inverse();

  // smoothed state vector
  Eigen::VectorXd x_n = x_f[k] + S * (x_s[0] - x_p[k]);

  // smoothed covariance matrix
  Eigen::MatrixXd P_n = P_f[k] + S * (P_s[0] - P_p[k]) * S.transpose();

  R << y->ex, 0, 0,
      0, y->et, 0,
      0, 0, y->ez;
//  R << y->ex, 0, 0,
//      0, y->et * std::sqrt(2), 0,
//      0, 0, y->ez; // INCREASING T ERROR ARTIFICIALLY
  R = R * R;

  Eigen::VectorXd Y(m);
  Y << y->x, y->t, y->z;

  // smoothed chi increment
  double chi_plus_s = (Y - C * x_n).transpose() * (R - C * P_n * C.transpose()).inverse().cwiseAbs() * (Y - C * x_n);

  // store smoothed data
  x_s.insert(x_s.begin(), x_n); //adding to beginner of vector x_s
  P_s.insert(P_s.begin(), P_n); //adding to beginner of vector P_s

  // smoothed velocity
  Eigen::VectorXd v(3);
  v << x_n[3], x_n[4], x_n[5];

  if (par_handler->par_map["debug"] == 1) {

    std::cout << "Smoothing: Residue is " << (Y - C * x_n).transpose() << " at y = " << y->y << " with chi " << chi_plus_s <<
        " updated velocity is " << v.transpose() / constants::c << " digi is " << Y.transpose() << std::endl;
  }

  double ndof = x_f.size();
  ndof = ndof > 1.0 ? 3.0 * ndof - 6.0 : 1.0;

  if (dropping
     && (ROOT::Math::chisquared_cdf(chi_plus_s, ndof) >= par_handler->par_map["kalman_pval_drop"]
     || !(par_handler->par_map["kalman_v_drop[0]"] < v.norm() / constants::c && v.norm() / constants::c < par_handler->par_map["kalman_v_drop[1]"])))
  {
    Eigen::MatrixXd K_n = P_n * C.transpose() * (-R + C * P_n * C.transpose()).inverse();

    x_n = x_n + K_n * (Y - C * x_n);
    P_n = (I - K_n * C) * P_n;

//    std::cout << "hit was dropped, new state is  " << x_n.transpose() << std::endl;
  }

  chi += chi_plus_s;

  return chi_plus_s;
}

void KalmanFilter::update_matrices(physics::digi_hit *a_hit)
{
  R << a_hit->ex, 0, 0,
      0, a_hit->et, 0,
      0, 0, a_hit->ez;
  R = R * R;

  if (initialized) {

    // Calculate the step size: distance to the previous hit in  vertical  direction
    dy = a_hit->y - y_val;
    // Calculate the uncertainty in step size.
    auto dt_sigma2 = (y_val_err*y_val_err + a_hit->ey*a_hit->ey)/x_hat[4]*x_hat[4]; // sigma_t**2 = sigma_y**2/vy**2

    f_matrix << 1.0, .0, .0, dy / x_hat[4], .0, .0,
                .0, 1.0, .0, .0, dy / (x_hat[4] * x_hat[4]), .0,
                .0, .0, 1.0, .0, .0, dy / x_hat[4],
                .0, .0, .0, 1.0, .0, .0,
                .0, .0, .0, .0, 1.0, .0,
                .0, .0, .0, .0, .0, 1.0;

    A << 1.0, .0, .0, dy / x_hat[4], -dy* x_hat[3]/(x_hat[4] * x_hat[4]), .0,
        .0, 1.0, .0, .0, -dy / (x_hat[4] * x_hat[4]), .0,
        .0, .0, 1.0, .0, -dy* x_hat[5]/(x_hat[4] * x_hat[4]), dy / x_hat[4],
        .0, .0, .0, 1.0, .0, .0,
        .0, .0, .0, .0, 1.0, .0,
        .0, .0, .0, .0, .0, 1.0;

    P_add << dt_sigma2*x_hat[3]*x_hat[3], .0, .0, .0, .0, .0,
             .0, dt_sigma2, .0, .0, .0, .0, 
             .0, .0, dt_sigma2*x_hat[5]*x_hat[5], .0, .0, .0, 
             .0, .0, .0, .0, .0, .0, 
             .0, .0, .0, .0, .0, .0, 
             .0, .0, .0, .0, .0, .0;    

    // direction cosines
    double a = x_hat[3] / constants::c;
    double b = x_hat[4] / constants::c;
    double c = x_hat[5] / constants::c;

    Q_update(dy, a, b, c);

    x_scat = std::sqrt(Q(0, 0)) * 100 / dy; // predicted std of scattering in x per y m
    z_scat = std::sqrt(Q(2, 2)) * 100 / dy; // predicted std of scattering in z per y m
  }


}

void KalmanFilter::Q_update(double dy, double a, double b, double c)
{
  // See MATHUSLA Calculations paper in ../docs/ for details

  // For the first hit, there is no scattering!
  if (x_p.size()==0){
    Q = Eigen::MatrixXd::Zero(6, 6);
    return;
  }

  double mag = std::sqrt(a*a + b*b + c*c);

  a /= mag; // normalise to 1 (ensures positive definite)
  b /= mag;
  c /= mag;

  //mag = std::sqrt(a*a + b*b + c*c); // just 1, only included incase above norm is removed
  //mag =1;

//  double sin_theta = std::sqrt(a*a + b*b) / mag; // sin(\theta) of track relative to orthogonal to layer
  double sin_theta = std::sqrt(b*b) / 1.0; // CORRECT ONE

  Q << dy * dy * (b * b + a * a) / std::pow(b, 4),
      dy * dy * a / (constants::c *mag * std::pow(b, 4)),
      dy * dy * a * c / std::pow(b, 4),
      constants::c *mag  * dy / b,
      -constants::c *mag  * dy * a / (b * b),
      0,
      dy * dy * a / (constants::c *mag  * std::pow(b, 4)),
      dy * dy * (1 - b * b) / (std::pow(constants::c *mag , 2) * std::pow(b, 4)),
      dy * dy * c / (constants::c *mag  * std::pow(b, 4)),
      dy * a / b,
      -dy * (1 - b * b) / (b * b),
      dy * c / b,
      dy * dy * a * c / std::pow(b, 4),
      dy * dy * c / (constants::c *mag  * std::pow(b, 4)),
      dy * dy * (c * c + b * b) / std::pow(b, 4),
      0,
      -constants::c *mag  * dy * c / (b * b),
      constants::c *mag  * dy / b,
      constants::c *mag  * dy / b,
      dy * a / b,
      0,
      std::pow(constants::c *mag , 2) * (1 - a * a),
      -std::pow(constants::c *mag , 2) * (a * b),
      -std::pow(constants::c *mag , 2) * (a * c),
      -constants::c *mag  * dy * a / (b * b),
      -dy * (1 - b * b) / (b * b),
      -constants::c *mag  * dy * c / (b * b),
      -std::pow(constants::c *mag , 2) * (a * b),
      std::pow(constants::c *mag , 2) * (1 - b * b),
      -std::pow(constants::c *mag , 2) * (b * c),
      0,
      dy * c / b,
      constants::c *mag  * dy / b,
      -std::pow(constants::c *mag , 2) * (a * c),
      -std::pow(constants::c *mag , 2) * (b * c),
      std::pow(constants::c *mag , 2) * (1 - c * c);

  //double sigma_ms = kalman::sigma_ms_p / kalman::p; // [rad]
  //double sigma_ms = par_handler->par_map["sigma_ms_p"] / par_handler->par_map["p"]; // [rad]

  double L_Al = detector::scintillator_height - detector::scintillator_thickness; // [cm] Aluminum
  //double L_Al = 0; // set aluminum to 0 for material interaction studies

  double L_Sc = detector::scintillator_thickness; // [cm] Scintillator

  double L_r_Sc = 43; // [cm] Radiation length Scintillator (Saint-Gobain paper)
  double L_r_Al = 24.0111; // [g cm^(-2)] Radiation length Aluminum

  double rho_Al = 2.7; // [g cm^(-3)] density of Aluminum
  L_r_Al /= rho_Al; // [cm]

  double L_rad = L_Al / L_r_Al + L_Sc / L_r_Sc; // [rad lengths] orthogonal to Layer

  L_rad /= sin_theta; // [rad lengths] in direction of track

  // double sigma_ms = 13.6 * std::sqrt(L_rad) * (1 + 0.038 * std::log10(L_rad)); // used to be standard ln (CORRECT ONE)
  double sigma_ms = 13.6 * std::sqrt(L_rad) * (1 + 0.038 * std::log(L_rad)); //
  sigma_ms /= par_handler->par_map["p"];

  Q = Q * std::pow(sigma_ms, 2); // Scattering contribution to process noise

}


void KalmanFilter::init_means(const Eigen::VectorXd x0, const Eigen::VectorXd q,
                              const Eigen::MatrixXd B, const Eigen::MatrixXd D)
{
  P = P0;
  chi = 0;

  A = I;

  x_hat = x0;

  initialized = true;

  x_f = {x_hat}; // Filter State
  P_f = {P};     // Filter Cov
  q_f = {q};
  q_s = {};
  D_f = {D};

  B_f = {B};
  C_f = {C};
  W_f = {};
  G_f = {};
  G_B_f = {};

  added_tracks = {};
}

double KalmanFilter::update_means(const std::vector<physics::track *> tracks)
{ // filter in tracks for vertexer in weighted means formalism

  int track_ind = this->find_nearest_vertex(tracks);

  if (track_ind == -1) // no tracks found
    return -1.0;

  unadded_tracks.clear();

  physics::track *new_track;
  for (int i = 0; i < tracks.size(); i++)
  {
    if (i == track_ind)
    {
      added_tracks.push_back(tracks[i]);
      new_track = tracks[i];
    }
    else
      unadded_tracks.push_back(tracks[i]);
  }

  Eigen::VectorXd y(6);
  y << new_track->x0, new_track->t0, new_track->z0, new_track->vx, new_track->vy, new_track->vz;

  update_matrices_means(new_track);

  Eigen::MatrixXd G = R.inverse(); // R is V in Fruhwirth

  Eigen::MatrixXd W = (B.transpose() * G * B).inverse();
  Eigen::MatrixXd G_B = G - G * B * W * B.transpose() * G;

  Eigen::MatrixXd P_new = (P.inverse() + C.transpose() * G_B * C).inverse();
  Eigen::MatrixXd D = W + W * B.transpose() * G * C * P_new * C.transpose() * G * B * W;

  Eigen::VectorXd x_hat_new = P_new * (P.inverse() * x_hat + C.transpose() * G_B * y);
  Eigen::VectorXd q = W * B.transpose() * G * (y - C * x_hat_new);

  Eigen::VectorXd res_t = (y - C * x_hat_new - B * q); // track residual
  Eigen::VectorXd res_v = (x_hat_new - x_hat);         // vertex residual

  double chi_plus = res_t.transpose() * G * res_t;

  chi_plus = chi_plus + res_v.transpose() * P.inverse() * res_v;

  chi += chi_plus;

  P_f.push_back(P_new);
  x_f.push_back(x_hat_new);
  q_f.push_back(q);
  y_v.push_back(y);

  D_f.push_back(D);
  B_f.push_back(B);
  C_f.push_back(C);
  W_f.push_back(W);
  G_f.push_back(G);
  G_B_f.push_back(G_B);

  x_hat = x_hat_new;
  P = P_new;

  return chi_plus;
}

void KalmanFilter::update_matrices_means(const physics::track *new_track)
{ // update matrices for vertexer

  Q_propagate(new_track);

  double dt = new_track->t0 - x_f.back()[3];

  B = Eigen::MatrixXd::Zero(6, 3);
  B << dt, 0, 0,
      0, dt / new_track->vy, 0,
      0, 0, dt,
      1, 0, 0,
      0, 1, 0,
      0, 0, 1;
}

void KalmanFilter::Q_propagate(const physics::track *new_track)
{ // update R using Q for vertex

  std::vector<std::vector<double>> y_s = detector::LAYERS_Y;

  double y_p = new_track->y0; // particle
  double y_v = x_hat[1];      // vertex

  // decide if vertex or lowest track hit is lower
  double y, y_min;

  if (y_p < y_v)
  {
    y = y_v;
    y_min = y_p;
  }
  else
  {
    y = y_p;
    y_min = y_v;
  }

  Eigen::MatrixXd R_temp = new_track->P_s;

  double a = new_track->vx / constants::c;
  double b = new_track->vy / constants::c;
  double c = new_track->vz / constants::c;

  // top layer index + 1
  int i = 9;

  while (y > y_min)
  {

    i--;

    if (i < 0)
      break;

    // current layer is higher than vertex
    // and lowest track layer
    if (y < y_s[i][1])
      continue;

    // y seperation of current and next layer
    double del_y = y - y_s[i][1];

    Q_update(del_y, a, b, c);

    /*
    *
    * R_temp still needs to be updated with Q
    *
    */

    // update y
    y = y_s[i][1];
  }

  R = R_temp;
}

void KalmanFilter::init_smooth_means()
{ // init function for the smoother

  x_n = x_f.back(); // x_k^n = x_n

  P_n = P_f.back(); // C_k^n = C_n (in Fruhwirth)

  q_s.push_back(q_f.back());

  alrdy_drop = false;
}

double KalmanFilter::smooth_means(int k)
{ // smooth the filtered data

  // smoothed vertex velocity
  Eigen::VectorXd q_n = W_f[k] * B_f[k].transpose() * G_f[k] * (y_v[k] - C_f[k] * x_n);

  // smoothed vertex velocity covariance matrix
  Eigen::MatrixXd D_n = W_f[k] + W_f[k] * B_f[k].transpose() * G_f[k] * C_f[k] * P_n *
                                     C_f[k].transpose() * G_f[k] * B_f[k] * W_f[k];

  // calculate the pull of speed from the speed of light
  double sigma_v = (q_n.transpose() * D_n * q_n)(0) / q_n.squaredNorm();
  double pull = (q_n.norm() - constants::c) / sigma_v;
  pulls_v_s.insert(pulls_v_s.begin(), pull);

  q_s.insert(q_s.begin(), q_n);
  D_s.insert(D_s.begin(), D_n);

  if (dropping &&
//      !(kalman::v_cut_drop[0] < q_n.norm() / constants::c && q_n.norm() / constants::c < kalman::v_cut_drop[1]))
      !(par_handler->par_map["v_cut_drop[0]"] < q_n.norm() / constants::c && q_n.norm() / constants::c < par_handler->par_map["v_cut_drop[1]"]))
  //      !(kalman::v_cut_drop[0] < pull && pull / constants::c < kalman::v_cut_drop[1]))
  {
    Eigen::MatrixXd P_n_new = (P_n.inverse() - C_f[k].transpose() * G_B_f[k] * C_f[k]).inverse();
    x_n = P_n_new * (P_n.inverse() * x_n - C_f[k].transpose() * G_B_f[k] * y_v[k]);
    P_n = P_n_new;
  }

  added_tracks[k]->q_s = q_n;
  added_tracks[k]->D_s = D_n;
  added_tracks[k]->q_f = q_f[k];
  added_tracks[k]->D_f = D_f[k];

  return 0;
}
