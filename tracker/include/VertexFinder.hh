#include "physics.hh"
#include "LinearAlgebra.hh"
#include "par_handler.hh"

#ifndef VF_H
#define VF_H

class vertex_seed
{
public:
	std::vector<double> seed_midpoint;
	std::vector<double> seed_midpoint_err = {1.0,1.0,1.0,1.0};
	double chi2;
	double closest_dist;
	double compatible_tracks; // Tom: add a counter for compatible tracks

	double score() { 
		// Previous score (2023.6.6)
		// return tracks.first->closest_approach(tracks.second); 

		// New score: distance [cm] - total_layers*100
		// auto score_distance = tracks.first->closest_approach(tracks.second);
		float score_layers = -100.0* (tracks.first->chi_s.size()+tracks.second->chi_s.size());
		return closest_dist + score_layers + chi2*10.0 - compatible_tracks*20.0;
		}

	std::pair<physics::track *, physics::track *> tracks;

	ParHandler* par_handler;

	vertex_seed() {}
	vertex_seed(physics::track *track1, physics::track *track2) : tracks(track1, track2)
	{
	}

	double closest_approach(physics::track *tr)
	{

		double ca1 = tracks.first->closest_approach(tr);
		double ca2 = tracks.second->closest_approach(tr);

		return ca1 < ca2 ? ca1 : ca2;
	}

	// vector::Vector guess()
	// {

	// 	return tracks.first->closest_approach_midpoint(tracks.second);
	// }
	std::vector<double> guess()
	{
		// Tom: changing from closest_approach_midpoint to closest_approach_midpoint_4d, because the former one does not give initial value of time
		return tracks.first->closest_approach_midpoint_4d(tracks.second);
	}	
	

	Eigen::VectorXd guess_k()
	{

		return tracks.first->ca_midpoint_kalman(tracks.second);
	}
};

class VertexFinder
{
public:
//	std::vector<physics::track *> tracks;
//	std::vector<physics::track *> tracks_k;
	std::vector<physics::track *> tracks_k_m;

//	std::vector<physics::vertex *> vertices;
//	std::vector<physics::vertex *> vertices_k;
	std::vector<physics::vertex *> vertices_k_m;

//	std::vector<vertex_seed> seeds;
//	std::vector<vertex_seed> seeds_k;
	std::vector<vertex_seed> seeds_k_m;

	ParHandler* par_handler;

	int missedChi2 = 0;
	int noConverge = 0;

	int noSeeds = 0;
	int failedSeed = 0;

//	void Seed();
//	void Seed_k();
	void Seed_k_m();

	void clear()
	{
//		tracks.clear();
//		tracks_k.clear();
		tracks_k_m.clear();

//		for (auto v : vertices)
//			delete v;
//		for (auto v : vertices_k)
//                      delete v;
		for (auto v : vertices_k_m)
		        delete v;

//		vertices.clear();
//		vertices_k.clear();
		vertices_k_m.clear();

//		seeds.clear();
//		seeds_k.clear();
		seeds_k_m.clear();

	}

//	void FindVertices();
//	void FindVertices_k_hybrid();
	void FindVertices_k();
	void FindVertices_k_m_hybrid();


}; //class VertexFinder

#endif
