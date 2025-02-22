#include "TrackFinder.hh"
#include "globals.hh"
#include "physics.hh"
#include <TMath.h>
#include <TRandom.h>
#include "TMinuit.h"
#include "statistics.hh"
#include "Geometry.hh"
#include "kalman.hh"
#include "kalman-test.hh"
#include <iostream>
#include <fstream>
#include <Eigen/Dense>
#include "par_handler.hh"
#include "Math/ProbFunc.h"

void TrackFinder::Seed()
{
//	seeds.clear();
//	seeds = {};
	for (int first = 0; first < hits_k.size(); first++)
	{
		for (int second = first + 1; second < hits_k.size(); second++)
		{

			int layer1 = ((hits_k[first])->det_id).layerIndex;
			int layer2 = ((hits_k[second])->det_id).layerIndex;

			if (layer1 == layer2)
				continue;
				
			// Excluding wall/floor hits
			if (layer1>detector::n_layers || layer2>detector::n_layers || layer1<cuts::include_floor.size() || layer2<cuts::include_floor.size() )
				continue;

			// Excluding seeds pairs that are within 3 layers
			if (TMath::Abs(hits_k[first]->y-hits_k[second]->y)<210)
				continue;

			double ds_2 = c_score(hits_k[first], hits_k[second]);
			double ds_2_rel = ds_2/(hits_k[first]->t - hits_k[second]->t)/(hits_k[first]->t - hits_k[second]->t);

			// std::cout << "Layer Index: " << layer1 <<", "<<layer2<<" c_score: "<< ds_2<< std::endl;


			//double dr = (hits[first].PosVector() - hits[second].PosVector()).Magnitude() / constants::c; // [ns]
			if (TMath::Abs(ds_2) > (par_handler->par_map["seed_interval"]))
				continue;
			if (ds_2_rel>0.1 | ds_2_rel<-0.25)
				continue;


			// Arrange the seed so that the first hit comes first in time
			if (hits_k[first]->t < hits_k[second]->t)
				seeds_k.push_back(seed(hits_k[first], hits_k[second]));
			else
				seeds_k.push_back(seed(hits_k[second], hits_k[first]));


			// seeds_k.back().score = TMath::Abs(ds_2_rel);
			seeds_k.back().score = ds_2_rel;

		} //"second" loop
	}	  //"first" loop

	// score_seeds();

} //TF::Seed

void TrackFinder::MergeTracks_k()
{
	//at this point, all of the points have been fit to tracks. At this point, we will perform the track merging step.

	if (tracks_k_m.size() == 0 or tracks_k_m.size() == 1)
		return;

	std::vector<int> deleted_tracks = {};

	Geometry geo;

	for (int first_track = 0; first_track < tracks_k_m.size(); first_track++)
	{

		for (int second_track = first_track + 1; second_track < tracks_k_m.size(); second_track++)
		{
			auto tr1 = tracks_k_m[first_track];
			auto tr2 = tracks_k_m[second_track];

			bool mergebool = false;

			auto first_layer = tr1->layers()[0] < tr2->layers()[0] ? tr1->layers()[0] : tr2->layers()[0];

			auto d1 = tr1->direction();
			auto d2 = tr2->direction();

			int tr1_missing_hits = 0;
			int tr2_missing_hits = 0;

			auto cos_theta = d1 ^ d2;

			double distance = tr1->closest_approach(tr2);

//			if (distance < cuts::merge_distance and cos_theta > cuts::merge_cos_theta){
			if (distance < par_handler->par_map["merge_distance"] and cos_theta > par_handler->par_map["merge_cos_theta"]){
				mergebool = true;
			}

			if ((tr1->_missing_layers.size() >= 3 or tr2->_missing_layers.size() >= 3) and (distance < 150 and cos_theta > 0.95)){
				//std::cout << "first holes" << std::endl;
				mergebool = true;
			}

			if ((tr1->_missing_layers.size() >= 2 or tr2->_missing_layers.size() >= 2) and (distance < 100 and cos_theta > 0.99)){
				//std::cout << "second holes" << std::endl;
				mergebool = true;
			}
			//at this point, we need to check if they have a certain number of missing hits

			//if (tr1->_missing_layers.size() < 3 or tr2->_missing_layers.size() < 3) continue;
			std::vector<int> joint_missing_hit_layers = {};
			for (int i = 0; i < tr1->_missing_layers.size(); i++)
			{
				auto layer = tr1->_missing_layers[i];
				if (layer < first_layer)
					continue;
				tr1_missing_hits++;

				bool missing = true;
				for (int j = 0; j < tr2->_missing_layers.size(); j++)
				{

					auto layer2 = tr2->_missing_layers[j];

					if (layer2 == layer)
					{
						missing = false;
						continue;
					}
				}

				if (missing)
					joint_missing_hit_layers.push_back(layer);
			}

			for (int i = 0; i < tr2->_missing_layers.size(); i++)
			{
				auto layer = tr2->_missing_layers[i];
				if (layer < first_layer)
					continue;
				tr2_missing_hits++;
				bool missing = true;
				for (int j = 0; j < tr1->_missing_layers.size(); j++)
				{

					auto layer2 = tr2->_missing_layers[j];

					if (layer2 == layer)
					{
						missing = false;
						continue;
					}
				}

				for (int j = 0; j < joint_missing_hit_layers.size(); j++)
				{

					auto layer2 = joint_missing_hit_layers[j];

					if (layer2 == layer)
					{
						missing = false;
						continue;
					}
				}

				if (missing)
					joint_missing_hit_layers.push_back(layer);
			}

			bool merge = false;

			if (joint_missing_hit_layers.size() < 3 and (tr1_missing_hits > 2 or tr2_missing_hits > 2))
				merge = true;

			//if (!merge) // only affects above condition
			//	continue;
			if (!mergebool){continue;} // from above loop

			//std::cout << "made it passed mergebool" << std::endl;

			Vector CA_mid = tr1->closest_approach_midpoint(tr2);
			if (!geo.inBox(CA_mid.x, CA_mid.y, CA_mid.z)) {
				std::cout << "CA is outside of detector" << std::endl;
				continue;
			}

			//std::cout << "made it passed CA" << std::endl;

			seed artificial_seed = seed(tr1->hits[0], tr1->hits[1]);

			// we've decided that the tracks should be merged
			// replace the tracks by a track made of a fit of their combined hits
			//
			// *** now we only take the hits from the first track
			// since the tracks are similar we can just use one track
			// *** the refit can eventually be taken out all together
			//for (auto hit : tr2->hits)
			//	tr1->AddHit(hit);

			kalman_track kft;
			kft.par_handler = par_handler;
			kft.finding = false;
			kft.dropping = false;
			kft.kalman_all(tr1->hits, &artificial_seed);

			deleted_tracks.push_back(second_track);

			if (kft.status != 2) // if fit fails, delete second track and don't change first
				continue;

			tr1->hits = kft.added_hits;

			tr1->x_scats = kft.x_scat;
			tr1->z_scats = kft.z_scat;

			tr1->chi_f = kft.chi_f;
			tr1->chi_s = kft.chi_s;

			tr1->estimate_list = kft.x_s_list;
			tr1->P_s = kft.P_s0;

			for (auto ind : tr2->king_move_inds) tr1->king_move_inds.push_back(ind);

		} //second track
	}	  //first track

	std::vector<physics::track *> good_tracks;

	for (int k = 0; k < tracks_k_m.size(); k++)
	{

		bool add = true;
		for (int del_index : deleted_tracks)
		{
			if (del_index == k)
				add = false;
		}

		if (add)
		{
			good_tracks.push_back(tracks_k_m[k]);
		}
	}

	tracks_k_m = good_tracks;

	//at this point, the list of tracks is finalized. Now they will be indexed:
	for (int trackn=0; trackn<tracks_k_m.size(); trackn++)
	{
		tracks_k_m[trackn]->index = trackn;
	}
	//file.close();
}

void TrackFinder::CalculateMissingHits(Geometry *geo)
{
	//calculate for kalman tracks
	for (auto track : tracks_k_m)
	{
		std::vector<int> layers = track->layers();
		std::vector<int> expected_layers;

		int layer_n = 0;
		for (auto layer_lims : detector::LAYERS_Y)
		{

			double y_center = (layer_lims[0] + layer_lims[1]) / 2.0;
			auto track_position = track->Position_at_Y(y_center);

			if (track_position.x > detector::x_min and track_position.x < detector::x_max)
			{
				if (track_position.z > detector::z_min and track_position.z < detector::z_max)
				{
					if (!(geo->GetDetID(track_position).IsNull()))
						expected_layers.push_back(layer_n);
				}
			}

			layer_n++;
		}

		track->SetExpectedLayers(expected_layers);

		std::vector<int> missing_layers;

		for (auto expected_index : expected_layers)
		{

			bool missing = true; //flag to indicate if the "expected_index" for the layer is missing or not

			for (auto existing_index : layers)
			{
				if (expected_index == existing_index)
					missing = false;
			}

			bool already_counted = false;
			if (missing)
			{
				for (auto _index : missing_layers)
				{
					if (expected_index == _index)
						already_counted = true;
				}

				if (!already_counted)
				{
					missing_layers.push_back(expected_index);
				}
			} //if missing
		}

		track->missing_layers(missing_layers);
	}
}

void TrackFinder::FindTracks_kalman()
{

	if (par_handler->par_map["debug"] == 1) std::cout << "Number of seeds:" <<  seeds_k.size() << std::endl;

	if (seeds_k.size() == 0)
		return; //no seeds found in initial seeding, will be retried with <c travel

	int index = 0;
	int total_hits = hits_k.size();
	bool iterate = true;
	int j = 0;
	int MAX_ITS = 25;

	Stat_Funcs sts;

	while (iterate)
	{
		if (seeds_k.size() == 0)
			break;
		if (hits_k.size() == 0)
			break;

		int min_index = min_seed_k();
		auto current_seed = seeds_k[min_index];

		seeds_k.erase(seeds_k.begin() + min_index); //delete the seed so that it isn't used again

		if (par_handler->par_map["debug"] == 1){
			std::cout << "New Seed ---------" <<current_seed.hits.first->index << ", " << current_seed.hits.second->index<< std::endl;
		}

		// check if the first seed hit is in the hit pool
		bool used = !seed_unused(current_seed); // double negative! used = not unused

		clear_vecs();

		std::vector<int> king_move_inds;

		// Construct the first filter (to find good hits from seed)
		kalman_track kf_find;
		kf_find.par_handler = par_handler;
		kf_find.finding = true;
		kf_find.dropping = true;
		kf_find.seed_was_used = used;

		if (par_handler->par_map["debug"] == 1) std::cout << "first fit" << std::endl;

		kf_find.kalman_all(hits_k, &current_seed);

		king_move_inds = kf_find.king_move_inds;

		if (kf_find.status == -1)
		{
			failure_reason[5] += 1;
			continue;
		}

		if (kf_find.status == -2)
                {
                        failure_reason[6] += 1;
                        continue;
                }

		undropped_hits = kf_find.found_hits;
		unused_hits = kf_find.unadded_hits;

		if (par_handler->par_map["debug"] == 1){ 		
			std::cout<<"Dropped (round1)"<<std::endl;
			for (auto hit: unused_hits) std::cout<<hit->index<<std::endl;
			std::cout<<"Found (round1)"<<std::endl;
			for (auto hit: undropped_hits) std::cout<<hit->index<<std::endl;		
		}

//		int drops = -1;
		int i = 0;

		bool failed = false;

//		while (drops != 0)
//		{
			kalman_track kft_;
			kft_.par_handler = par_handler;
        	        kft_.finding = false;
                	kft_.dropping = true;
	                kft_.seed_was_used = used;
					kft_.unadded_hits = unused_hits;

			if (par_handler->par_map["debug"] == 1) std::cout << "second fit" << std::endl;

                	kft_.kalman_all(undropped_hits, &current_seed);


			if (kft_.status == -1)
        	{
				failed = true;
	                        continue;
        	}

			good_hits = kft_.added_hits;

			undropped_hits.clear();

			if (kft_.status == -2)
			{
				failed = true;
				continue;
			}

			if (good_hits.size() < cuts::track_nlayers)
			{
				failure_reason[4] += 1;
			}

			unused_hits = kft_.unadded_hits;
			unused_hits.insert(unused_hits.end(), kf_find.unadded_hits.begin(), kf_find.unadded_hits.end());

			if (par_handler->par_map["debug"] == 1){ 		
				std::cout<<"+Dropped (round2)"<<std::endl;
				for (auto hit: kft_.unadded_hits) std::cout<<hit->index<<std::endl;
				std::cout<<"Found (round2)"<<std::endl;
				for (auto hit: good_hits) std::cout<<hit->index<<std::endl;		
			}			

//			int drops = 0;

			double ndof = good_hits.size();
			// During dropping, the DOF for each step is equal to the dimension of the measurement.
			ndof = 3.0;

			//dropping hits
			for (int n = 0; n < good_hits.size(); n++)
			{
				// make an eigenvector for the velocity at the lowest (in y) state of the track
				Eigen::VectorXd v(3);
				v << kft_.v_s_list[n][0], kft_.v_s_list[n][1], kft_.v_s_list[n][2];


				// if (ROOT::Math::chisquared_cdf(kft_.chi_s[n], ndof) >= par_handler->par_map["kalman_pval_drop"]
			    //        || !(par_handler->par_map["kalman_v_drop[0]"] < v.norm() / constants::c
				//    && v.norm() / constants::c < par_handler->par_map["kalman_v_drop[1]"]))
				bool DROPPED = false;

				if (good_hits[n]->det_id.isWallElement || good_hits[n]->det_id.isFloorElement){
					DROPPED=(ROOT::Math::chisquared_cdf(kft_.chi_s[n], ndof) >= par_handler->par_map["kalman_pval_drop_floorwall"]);
				}
				else if ((ROOT::Math::chisquared_cdf(kft_.chi_s[n], ndof) >= par_handler->par_map["kalman_pval_drop"]))
					//  || !(par_handler->par_map["kalman_v_drop[0]"] < v.norm() / constants::c && v.norm() / constants::c < par_handler->par_map["kalman_v_drop[1]"])))
				{
					DROPPED=true;
				}		

				if (DROPPED)		
				{
					if (par_handler->par_map["debug"] == 1) {
						std::cout << "hit"<< good_hits[n]->index <<" dropped with y " << good_hits[n]->y << " chi_cdf " << ROOT::Math::chisquared_cdf(kft_.chi_s[n], ndof) <<
							" v " << v.norm() / constants::c << std::endl;
					}
					unused_hits.push_back(good_hits[n]);
//					drops++;
				}
				else
				{
					undropped_hits.push_back(good_hits[n]);
				}
			}

			if (undropped_hits.size() < cuts::track_nlayers)
			{
				failed = true;
//				break;
				continue;
			}

		//} // dropping while loop

		// Do a final hit with bad hits removed
		kalman_track kft_2;
		kft_2.par_handler = par_handler;
		kft_2.finding = false;
		kft_2.dropping = false;

		if (par_handler->par_map["debug"] == 1) std::cout << "third fit" << std::endl;

		kft_2.kalman_all(undropped_hits, &current_seed);

		if (kft_2.status != 2)
		{
			continue;
		}

		std::vector<double> diag_cov;
		for (int i = 0; i < 7; i++) diag_cov.push_back(kft_2.track_cov[i][i]);

		//auto current_track = new physics::track(kft_2.x_s, kft_2.P_s);
		auto current_track = new physics::track(kft_2.x_s, diag_cov);
		current_track->hits = undropped_hits;

		current_track->x_scats = kft_2.x_scat;
		current_track->z_scats = kft_2.z_scat;

		current_track->chi_f = kft_2.chi_f;
		current_track->chi_s = kft_2.chi_s;

		current_track->estimate_list = kft_2.x_s_list;
		current_track->P_s = kft_2.P_s0;

		current_track->king_move_inds = kft_2.king_move_inds;

		/*
		static double cov_matrix[7][7];
		for (int i = 0; i < 7; i++)
		{
			for (int j = 0; j < 7; j++)
			{
				//cov_matrix[i][j] = kft_2.track_cov[i][i];
				//if (i == j)
				//{
				//	cov_matrix[i][j] = std::pow(kft_2.P_s[i],2);
				//}
				else
				{
					cov_matrix[i][j] = 0;
				}

			}
		}
		*/
		//current_track->CovMatrix(cov_matrix, 7);

		//static double cov_matrix[7][7];
		//cov_matrix = kft_2.track_cov;
		current_track->CovMatrix(kft_2.track_cov, 7);

		//double ndof = kft_2.chi_s.size();
		ndof = kft_2.chi_s.size();
		ndof = ndof > 1.0 ? 3.0 * ndof - 4.0 : 1.0;

		for (auto chi : kft_2.chi_f) {
			local_chi_f.push_back(chi);

			//double p_val = sts.chi_prob(chi,ndof);
                        //local_chi_f.push_back(p_val);
		}
		local_chi_f.push_back(-1);

		for (auto chi : kft_2.chi_s) {
			local_chi_s.push_back(chi);

			//double p_val = sts.chi_prob(chi,ndof);
			//local_chi_s.push_back(p_val);
		}
		local_chi_s.push_back(-1);

		// calculate chi per ndof from sum of chi increments in the track
		double chi_sum = 0;
		for (auto chi : kft_2.chi_s)
			chi_sum += chi;

		//chi_sum = chi_sum / (4.0 * kft_2.chi_s.size() - 6.0); // always non_negative due to num_hit cut (if track can be passed)
		// chi_sum = chi_sum / ndof;

		// Calculate the chi2 p value, and check if it has wall/floor hit
		double chi2_cdf = ROOT::Math::chisquared_cdf(chi_sum, ndof);
		current_track->chi2_pval = chi2_cdf;
		bool IsFloorWallTrack=false;
		for (auto hit: current_track->hits){
			if (hit->det_id.isFloorElement || hit->det_id.isWallElement){
				IsFloorWallTrack=true;
				break;
			}
		}
		current_track->IsFloorWallTrack=IsFloorWallTrack;

		if (current_track->nlayers() >= cuts::track_nlayers && (chi2_cdf<= par_handler->par_map["kalman_pval_track"]))
		{
			tracks_k.push_back(current_track);

			if (par_handler->par_map["debug"] == 1) std::cout << "Track made it" << std::endl;
		}
		// If it is a track with floor/wall hits, ignore the chi2 cut
		else if(chi2_cdf> par_handler->par_map["kalman_pval_track"])
		{  
			if (IsFloorWallTrack){
				tracks_k.push_back(current_track);
				if (par_handler->par_map["debug"] == 1) std::cout << "Track made it (wall/floor track, no chi2 cut)" << std::endl;				
			}
			else{
				if (par_handler->par_map["debug"] == 1) std::cout << "Track failed, large chi2. Chi2/dof: " << chi_sum << "/" << ndof <<", p=" << chi2_cdf<< std::endl;
				delete current_track;
				continue;	
			}
			
		}
		else
		{
			
			if (current_track->nlayers() < cuts::track_nlayers && (par_handler->par_map["debug"] == 1))
				std::cout << "Track failed, not enough hits. N hits " << current_track->nlayers() << std::endl;

			// if ((chi2_cdf> par_handler->par_map["kalman_pval_track"]) && (par_handler->par_map["debug"] == 1))
			// 	std::cout << "Track failed, large chi2. Chi2/dof: " << chi_sum << "/" << ndof <<", p=" << chi2_cdf<< std::endl;

			delete current_track;
			continue;
		}


		if (failed) continue;

		hits_k = unused_hits;

		if (seeds_k.size() == 0)
			{iterate = false; break;}
		if (hits_k.size() < cuts::nseed_hits)
			{iterate = false; break;}

		// Remove seeds with hits that are already used in tracks
		// First, get a list of ID of remaining hits
		std::vector <int> hits_k_ids;
		for (auto hit : hits_k){
			hits_k_ids.push_back(hit->index);
		}
		// Then, loop all the seeds and erase the ones that does not appear in the remaining hits.	
		for (int i = seeds_k.size() - 1; i >= 0; i--){
			auto current_seed = seeds_k.at(i);
			std::vector<int>::iterator find1, find2;
			find1 = std::find(hits_k_ids.begin(), hits_k_ids.end(),current_seed.hits.first->index);
			find2 = std::find(hits_k_ids.begin(), hits_k_ids.end(),current_seed.hits.second->index);
			// if either hit in the seeds is not found in the remaining hits:
			if ((find1 == hits_k_ids.end()) || (find2 == hits_k_ids.end())){
				seeds_k.erase(seeds_k.begin() + i);
			}
		}
	}
	// assign indices
	for (int i=0; i < tracks_k.size(); i++)
        {
                tracks_k[i]->index = i;
        }

}
