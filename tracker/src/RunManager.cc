#include <iostream>
#include "RunManager.hh"
#include "TreeHandler.hh"
#include "TrackFinder.hh"
#include "Digitizer.hh"
#include "globals.hh"
#include <iostream>
#include <fstream>
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include "NoiseMaker.hh"
#include "par_handler.hh"

int RunManager::StartTracking()
{
	TreeHandler _handler(_InputTree_Name, _InputFile_Name, _OutputTree_Name, OutFileName());
	if (_handler.IsNull()) {
		std::cout << "Sorry, I couldn't open that file" << std::endl;
		return 0;
	} 

	TH = &_handler;
	int events_handled = 0;

	int made_its_k = 0;

	int events_w_k_tracks = 0;

	int verts_k_m = 0;

	int dropped_hits = 0;
	int floor_wall_hits = 0;

	std::vector<int> zeros(9, 0);
	std::vector<int> failures_k = zeros;

	ParHandler hndlr;
	hndlr.Handle();

	if (hndlr.par_map["branch"] == 1.0) std::cout << "Running in Cosmic Mode" << std::endl;

	_digitizer->par_handler = &hndlr;
	_tracker->par_handler = &hndlr;
	_vertexer->par_handler = &hndlr;

	NoiseMaker::preDigitizer();

	_digitizer->InitGenerators();

	while (TH->Next() >= 0)
	{
		if (events_handled >= hndlr.par_map["end_ev"]) //cuts::end_ev)
		{
			break;
		}
		if (events_handled >= hndlr.par_map["start_ev"]) //cuts::start_ev)
		{

			if (hndlr.par_map["debug"] == 1) 
				std::cout << "\n"<< std::endl;
			if ((events_handled) % 1000 == 0 || hndlr.par_map["debug"] == 1 || hndlr.par_map["debug_vertex"] == 1 )
				std::cout << "=== Event is " << events_handled <<" ==="<< std::endl;

			TotalEventsProcessed++;
			_digitizer->clear();
			_tracker->clear();
			_vertexer->clear();

			// copying the data to the new tree, and loading all the variables, incrementing index
			TH->LoadEvent();

			//adding all hits of the tree into the digitizer
			for (int n_hit = 0; n_hit < TH->sim_numhits; n_hit++)
			{
				physics::sim_hit *current = new physics::sim_hit(TH, n_hit);
				if (hndlr.par_map["branch"] == 1.0) {
					current->x += detector::COSMIC_SHIFT[0];
					current->y += detector::COSMIC_SHIFT[1];
					current->z += detector::COSMIC_SHIFT[2];
				}
				_digitizer->AddHit(current);
			}

			_digitizer->ev_num = events_handled;
			std::vector<physics::digi_hit *> digi_list = _digitizer->Digitize();

			TH->ExportDigis(digi_list, _digitizer->seed);

			// digis now finished and stored in tree!!!
			// now, we begin the seeding algorithm

			// remove this carefully in TrackFinder.cc
			_tracker->failure_reason = zeros;

			_tracker->hits_k = digi_list;
			_tracker->Seed();
			_tracker->FindTracks_kalman();

			// copy kalman tracks for merging
			for (auto t : _tracker->tracks_k)
			{
				physics::track *temp = new physics::track(*t);
				_tracker->tracks_k_m.push_back(temp);
			}

			if (hndlr.par_map["merge_cos_theta"] != -2) {
				_tracker->CalculateMissingHits(_digitizer->_geometry);
				_tracker->MergeTracks_k();
			}

			_tracker->CalculateMissingHits(_digitizer->_geometry);

			made_its_k += _tracker->tracks_k_m.size();

			TH->ExportTracks_k_m(_tracker->tracks_k_m);

			_vertexer->tracks_k_m = _tracker->tracks_k_m;

			_vertexer->Seed_k_m();
			_vertexer->FindVertices_k_m_hybrid();

			//_vertexer->FindVertices_k();

			verts_k_m += _vertexer->vertices_k_m.size();

			TH->ExportVertices_k_m(_vertexer->vertices_k_m);

			TH->Fill();

			if (_tracker->tracks_k_m.size() > 0) events_w_k_tracks++;

			dropped_hits += _digitizer->dropped_hits;
			floor_wall_hits += _digitizer->floor_wall_hits;

		}

		events_handled++;
	}

	

	std::cout<<" Vertex failure count\n  noSeeds: "<<_vertexer->noSeeds<<std::endl;
	std::cout<<"  not converge: "<<_vertexer->noConverge<<std::endl;
	std::cout<<"  rejected by chi2 cut: "<<_vertexer->missedChi2<<std::endl;
	std::cout<<"  not enough tracks: "<<_vertexer->failedSeed<<std::endl;

	if (hndlr.file_opened) {
		std::cout << made_its_k << " Kalman tracks made it" << std::endl;
		std::cout << verts_k_m << " Merged Kalman vertices made it" << std::endl;
		std::cout << events_w_k_tracks << " Events had a kalman track" << std::endl;

		TH->Write();

		std::cout << "Tracked " << TotalEventsProcessed << " Events" << std::endl;

//		std::cout << "# Dropped Hits / # Floor Wall Hits = " << (double) dropped_hits / floor_wall_hits;

	}

	return 0;
}
