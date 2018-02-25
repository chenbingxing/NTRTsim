/*
 * Copyright © 2012, United States Government, as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All rights reserved.
 *
 * The NASA Tensegrity Robotics Toolkit (NTRT) v1 platform is licensed
 * under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0.
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language
 * governing permissions and limitations under the License.
*/

/**
 * @file AppLaikaCombinedMotion.cpp
 * @brief Contains the definition function main() for AppLaikaCombinedMotion
 * which builds a horizontal spine structure defined in YAML, with a rotating vertebra
 * @author Drew Sabelhaus
 * $Id$
 */

// This application
#include "yamlbuilder/TensegrityModel.h"
#include "CombinedSpineControllerBending.h"
#include "CombinedSpineControllerRotVertPositionTraj.h"
// This library
#include "core/terrain/tgBoxGround.h"
#include "core/tgModel.h"
#include "core/tgRod.h"
#include "core/tgSimulation.h"
#include "core/tgSimViewGraphics.h"
#include "core/tgWorld.h"
#include "core/abstractMarker.h"
// For tracking positions:
#include "sensors/tgDataLogger2.h"
#include "sensors/tgSphereSensorInfo.h"
#include "sensors/abstractMarkerSensorInfo.h"
// Correlating positions to cable lengths.
#include "sensors/tgSpringCableActuatorSensorInfo.h"
//DEBUGGING: see if the rod COMs, for the shoulders/hips, are the same as
// for the spheres. That would mean that bullet's COM command does the COM
// for the ENTIRE rigid, not each component, when auto-compounded!!!
#include "sensors/tgRodSensorInfo.h"
// Bullet Physics
#include "LinearMath/btVector3.h"
#include "core/tgWorldBulletPhysicsImpl.h" // for hinge hack
#include "BulletDynamics/ConstraintSolver/btHingeConstraint.h" // for hinge hack
#include "BulletDynamics/Dynamics/btDynamicsWorld.h" // for hinge hack
// The C++ Standard Library
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept> // for some error handling


// A helper function for converting doubles to strings, for filenames.
// Thanks to http://www.cplusplus.com/articles/D9j2Nwbp/
template <typename T>
std::string NumberToString ( T Number )
{
  std::ostringstream ss;
  ss << Number;
  return ss.str();
}

/**
 * The entry point.
 * @param[in] argc the number of command-line arguments
 * @param[in] argv argv[0] is the executable name
 * @param[in] argv argv[1] is the path of the YAML encoded structure
 * @return 0
 */
int main(int argc, char** argv)
{
    // For this YAML parser app, need to check that an argument path was
    // passed in.
    if (argv[1] == NULL)
    {
      throw std::invalid_argument("No arguments passed in to the application. You need to specify which YAML file you would like to build.");
    }
  
    // create the ground and world. Specify ground rotation in radians
    const double yaw = 0.0;
    const double pitch = 0.0;
    const double roll = 0.0;
    const tgBoxGround::Config groundConfig(btVector3(yaw, pitch, roll));
    // the world will delete this
    tgBoxGround* ground = new tgBoxGround(groundConfig);

    // We're working in centimeters, so scaling factor = 100.
    const tgWorld::Config config(9.81); // gravity, dm/sec^2 is 98.1.
    // As of the commit when this comment appears, the units are still
    // a bit off. Gravity and cable force are still with s=10 (decimeters
    // and 1/10 factor on pretensions), but the length and density
    // units are for s=100 (centimeters).
    // Drew believes this is still kinematically valid, though,
    // since we're not recording force, but instead are recording
    // (effectively) the center-of-mass shift with respect to the spine.
    tgWorld world(config, ground);

    // create the view
    const double timestep_physics = 0.0001; // seconds
    //const double timestep_physics = 0.001;
    const double timestep_graphics = 1.f/60.f; // seconds
    tgSimViewGraphics view(world, timestep_physics, timestep_graphics);

    // create the simulation
    tgSimulation simulation(view);

    // create the models with their controllers and add the models to the simulation
    // This constructor for TensegrityModel takes the 'debugging' flag as the
    // second argument.
    TensegrityModel* const myModel = new TensegrityModel(argv[1],true);

    /**
     * FOR IROS 2018 DATA COLLECTION
     * Specify which foot should be lifted. 
     * This way, we can parameterize data collection more easily.
     */
    
    // String specifier of which foot to lift. 1, 2, 3, 4 are A,B,C,D.
    // Correspond to FR, FL, BR, BL.
    int whichFoot = 2;
    
    // switch on the string and assign the following:
    // (1) parameters for the horizontal bending controller
    // (2) parameters for the rotation controller, including data collection
    // we'll make the log file name also include min length percent.

    // first, declare everything we need.
    // Some of these are initialized to prevent segfaults accidentally
    // Rotating vertebra controller + data logging:
    std::string footDataFilePrefix;
    double timeInterval = 0.0;
    double startTimeRot = 0.0;
    std::string csvPath;
    double KP = 0.0; // proportional control const for rot ver pos tracking
    double KI = 0.0; // integral control const for rot ver pos tracking
    double KD = 0.0; // deriv control const for rot vert pos tracking
    // Horizontal bending controller
    double startTimeBend = 0.0;
    double minLength = 0.0;
    double rate = 0.0;
    std::vector<std::string> tagsToControl;
    
    switch (whichFoot) {

      case 1:
	// FR
	// bend:
	startTimeBend = 2.0;
	minLength = 0.9; // as a percent
	rate = 0.25;
	// front right foot requires COM shifting to the left
	// Pulling in the right cable shifts robot to the left.
	tagsToControl.push_back("HR");
	// rot:
	footDataFilePrefix = "~/NTRTsim_logs/LaikaIROS2018MarkerDataA_" +
	  NumberToString(minLength);
	timeInterval = 1; // was 0.01 for data collection.
	startTimeRot = 15.0; // was 8.0
	//csvPath = "../../../../src/dev/laika/v0.2_combined/setpoint_trajectories/motor_data_ramp_dt01_tt_20_max_neg_pi8.csv";
	// that rotation might just be too dynamic for laika (tips over.)
	csvPath = "../../../../src/dev/laika/v0.2_combined/setpoint_trajectories/motor_data_ramp_dt01_tt_20_max_neg_pi32.csv";
	KP = 0.2;
	KI = 0.000001;
	KD = 0.2;
	break;
      
      case 2:
	// FL
	// bend:
	startTimeBend = 2.0;
	minLength = 0.9; // as a percent
	rate = 0.25;
	// front left foot requires COM shifting to the right
	// Pulling in the left cable shifts robot to the right.
	tagsToControl.push_back("HL");
	// rot:
	footDataFilePrefix = "~/NTRTsim_logs/LaikaIROS2018MarkerDataB_" +
	  NumberToString(minLength);
	timeInterval = 0.01; // was 0.01 for data collection.
	startTimeRot = 15.0; // was 8.0
	csvPath = "../../../../src/dev/laika/v0.2_combined/setpoint_trajectories/motor_data_ramp_dt01_tt_20_max_pi8.csv";
	// The original, with lots of rotation, was:
	//csvPath = "../../../../src/dev/laika/v0.2_combined/setpoint_trajectories/motor_data_ramp_dt01_tt_10_max_05.csv";
	KP = 0.01; // was 0.05
	//KI = 0.00001; // was 0.00001
	KI = 0.0;
	KD = 0.0;
	break;

    case 3:
	// BR
      	// bend:
	startTimeBend = 2.0;
	minLength = 0.9; // as a percent
	rate = 0.25;
	// back right foot requires bending to the left
	// pulling on the right cables shifts COM to the left
	tagsToControl.push_back("HR");
	// rot:
	footDataFilePrefix = "~/NTRTsim_logs/LaikaIROS2018MarkerDataC_"+
	  NumberToString(minLength);
	timeInterval = 1; // was 0.01 for data collection.
	startTimeRot = 15.0; // was 8.0
	csvPath = "../../../../src/dev/laika/v0.2_combined/setpoint_trajectories/motor_data_ramp_dt01_tt_20_max_pi8.csv";
	KP = 0.5;
	KI = 0.00001;
	KD = 0.0;
	break;

    case 4:
	// BL
	// bend:
	startTimeBend = 2.0;
	minLength = 0.9; // as a percent
	rate = 0.25;
	// back left foot requires COM shifting to the right
	// Pulling in the left cable shifts robot to the left.
	tagsToControl.push_back("HL");
	// rot:
	footDataFilePrefix = "~/NTRTsim_logs/LaikaIROS2018MarkerDataD_" +
	  NumberToString(minLength);
	timeInterval = 1; // was 0.01 for data collection.
	startTimeRot = 15.0; // was 8.0
	//csvPath = "../../../../src/dev/laika/v0.2_combined/setpoint_trajectories/motor_data_ramp_dt01_tt_20_max_neg_pi8.csv";
	// that rotation might just be too dynamic for laika (tips over.)
	csvPath = "../../../../src/dev/laika/v0.2_combined/setpoint_trajectories/motor_data_ramp_dt01_tt_20_max_neg_pi32.csv";
	KP = 0.2;
	KI = 0.000001;
	KD = 0.2;
	break;
	
      default:
	// need to throw an error here
	throw std::invalid_argument("Need to specify which foot to lift.");
    }

    /**
     * ALSO FOR IROS 2018 DATA COLLECTION
     * For generating figures of the robot lifting its feet, we need to move it around a bit.
     * Which means ...
     * ...maybe I don't have to do this, just translate the robot by half its X,
     * and move the markers that way also.
     */

    // Attach a controller to the model, if desired.
    // This is a controller that interacts with a generic TensegrityModel as
    // built by the TensegrityModel file, BUT it only actually works
    // with the specific HorizontalSpine YAML file.
    // @TODO: should this throw an error when attached to a model that
    // wasn't built with the HorizontalSpine YAML file?

    // Parameters for the Horizontal Spine Controller are specified in that .h file,
    // repeated here:
    //double startTime = 5.0;
    //double minLength = 0.8;
    //double rate = 0.25;
    //std::vector<std::string> tagsToControl;
    // HF is the right horizontal set
    // HL is the bottom horizontal set maybe?
    // HB is the left horizontal set
    // HR is the top horizontal set.
    // BUT, something is wrong here. Probably Bullet's numerical problems.
    //tagsToControl.push_back("HR");
    //tagsToControl.push_back("testnone");
    //tagsToControl.push_back("HF");
    //tagsToControl.push_back("HB");
    // Call the constructor for the controller
    CombinedSpineControllerBending* const controller =
      new CombinedSpineControllerBending(startTimeBend, minLength, rate, tagsToControl);
    // Attach the controller to the model. Must happen before running the
    // simulation.
    // TEMPORARILY: can we get a leg to lift, in the SI units model?
    //myModel->attach(controller);

    // Next, we need to get a reference to the Bullet Physics world.
    // This is for passing in to the CombinedSpineControllerRotVertPositionTraj, so it can
    // create the hinge.
    // TO-DO: does this reference get destroyed and re-created?? this will break...
    tgWorld simWorld = simulation.getWorld();
    tgWorldImpl& impl = world.implementation();
    tgWorldBulletPhysicsImpl& bulletWorld = static_cast<tgWorldBulletPhysicsImpl&>(impl);
    btDynamicsWorld* btWorld = &bulletWorld.dynamicsWorld();

    // Create the controller for the rotating vertebra.

    // For the trajectory tracking: need a CSV file.
    //std::string csvPath = "../../../../src/dev/laika/v0.2_combined/setpoint_trajectories/motor_data_ramp_dt01_tt_10_max_05.csv";
    
    std::string rodHingeTag = "rodForHinge";
    // NOTE THAT THIS CONTROLLER ALSO LOGS MARKER DATA.
    CombinedSpineControllerRotVertPositionTraj* rotController =
      new CombinedSpineControllerRotVertPositionTraj( startTimeRot, csvPath,
						      rodHingeTag, footDataFilePrefix,
						      timeInterval, KP, KI, KD,
						      btWorld);

    // Add the controller to the YAML model.
    // TO-DO: can we do this after adding the model to the simulation?
    // Will the controller's onSetup method still be
    myModel->attach( rotController );

    // Add the model to the world
    simulation.addModel(myModel);

    // Let's try to add markers at the feet.
    std::cout << "getting the rigid body for front: " << std::endl;
    // For TetrahedralSpineWithRotVert:
    //std::vector<tgBaseRigid*> frontRigids = myModel->find<tgBaseRigid>("shouldersBack");
    // For LaikaIROS2018:

    // For the front (feet A/B):
    std::vector<tgBaseRigid*> frontRigids = myModel->find<tgBaseRigid>("shouldersVertConnect");
    if( frontRigids.size() == 0){
      std::cout << "Warning! Did not find any rigids to put markers on!" << std::endl;
    }
    for(int jj=0; jj < frontRigids.size(); jj++) {
      std::cout << "Picked out a front rigid with com: "
		<< frontRigids[jj]->centerOfMass() << std::endl;
    }
    
    // For the rear (feet C/D):
    std::cout << "getting the rigid body for rear: " << std::endl;
    std::vector<tgBaseRigid*> rearRigids = myModel->find<tgBaseRigid>("hipsVertConnect");
    if( rearRigids.size() == 0){
      std::cout << "Warning! Did not find any rigids to put markers on!" << std::endl;
    }
    for(int jj=0; jj < rearRigids.size(); jj++) {
      std::cout << "Picked out a rear rigid with com: "
		<< rearRigids[jj]->centerOfMass() << std::endl;
    }
    // Add a marker in the global world. Subtract away the COM of the rigid
    // body, since markers seemed to be referenced against tha
    //btVector3 offsetFootA(0, 0, 0);
    // For the generic TetrahedralSpineWithRotJoint, feet are at
    // ( -33 / +25, 5, +/- 15)
    // For the Laika IROS 2018 model, in cm, feet are at: (0 for hips)
    // quick calculation: front X is -52.201, -12.6 for leg offset, = -64.801
    // ( 0 / -64.801 , 1.5, +/- 7.55)
    
    //btVector3 offsetFootA(-33, 5, 15);
    // Foot A/B at -x. Foot B/C at -y. (front is -x, right is -y.)
    // FOR FINAL_LAIKA: translated the whole robot so it's centered.
    // That adds 33.0255 to all X positions.
    // SO, -64.801 + 33.0255 = -31.7755
    // and then + that number for the others.
    /*
    btVector3 offsetFootA( -31.7755, 1.5, -7.55);
    btVector3 offsetFootB( -31.7755, 1.5, 7.55);
    btVector3 offsetFootC( 31.7755, 1.5, -7.55);
    btVector3 offsetFootD( 31.7755, 1.5, 7.55);
    */
    // For the Laika with SI units, convert all down to meters.
    // The 'dropped' model is moved up by 1.55 meters.
    btVector3 offsetFootA( -0.64801, 1.55, -0.0755);
    btVector3 offsetFootB( -0.64801, 1.55, 0.0755);
    btVector3 offsetFootC( 0.64801, 1.55, -0.0755);
    btVector3 offsetFootD( 0.64801, 1.55, 0.0755);
    // Specify some colors for each marker.
    btVector3 colorA( 0.5, 0.5, 0.5);
    btVector3 colorB( 0.2, 0.7, 0.2);
    btVector3 colorC( 0.5, 0.2, 0.0);
    btVector3 colorD( 0.0, 0.7, 0.7);

    abstractMarker markerA(frontRigids[0]->getPRigidBody(),
			   offsetFootA - frontRigids[0]->getPRigidBody()->getCenterOfMassPosition(),
			   colorA, 0);
    abstractMarker markerB(frontRigids[0]->getPRigidBody(),
			   offsetFootB - frontRigids[0]->getPRigidBody()->getCenterOfMassPosition(),
			   colorB, 0);
    abstractMarker markerC(rearRigids[0]->getPRigidBody(),
			   offsetFootC - rearRigids[0]->getPRigidBody()->getCenterOfMassPosition(),
			   colorC, 0);
    abstractMarker markerD(rearRigids[0]->getPRigidBody(),
			   offsetFootD - rearRigids[0]->getPRigidBody()->getCenterOfMassPosition(),
			   colorD, 0);
    myModel->addMarker(markerA);
    myModel->addMarker(markerB);
    myModel->addMarker(markerC);
    myModel->addMarker(markerD);
	   
    // Let's log info from the spheres (bottom of Laika's feet.)
    // EDIT: UNUSED, LOGGING OCCURS IN CONTROLLER NOW.
    
    // has to end with the prefix to the log file name.
    //std::string log_filename = "~/NTRTsim_logs/LaikaCombinedMotion";
    //double samplingTimeInterval = 0.1;
    //tgDataLogger2* myDataLogger = new tgDataLogger2(log_filename, samplingTimeInterval);
    // add the model to the data logger
    //myDataLogger->addSenseable(myModel);
    // Make it so the data logger can dispatch sphere sensors
    //abstractMarkerSensorInfo* myAbstractMarkerSensorInfo = new abstractMarkerSensorInfo();
    // Correlating the cable lengths to foot positions.
    //tgSpringCableActuatorSensorInfo * mySCASensorInfo = new tgSpringCableActuatorSensorInfo();
    //tgSphereSensorInfo* mySphereSensorInfo = new tgSphereSensorInfo();
    //DEBUGGING: rods too
    //tgRodSensorInfo* myRodSensorInfo = new tgRodSensorInfo();
    //myDataLogger->addSensorInfo(myAbstractMarkerSensorInfo);
    //myDataLogger->addSensorInfo(mySCASensorInfo);
    //myDataLogger->addSensorInfo(mySphereSensorInfo);
    //myDataLogger->addSensorInfo(myRodSensorInfo);
    // Add the data logger to the simulation.
    //simulation.addDataManager(myDataLogger);
    
    // Finally, run the simulation.
    simulation.run();

    // teardown is handled by delete
    return 0;
}