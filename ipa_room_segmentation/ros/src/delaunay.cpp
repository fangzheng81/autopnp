#include "ros/ros.h"
#include "ipa_room_segmentation/seg.h"
#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <iostream>
#include <list>
#include <vector>
#include <math.h>

#define PI 3.14159265

bool contains(std::vector<cv::Point> vector, cv::Point element)
{
	return vector.end() != std::find(vector.begin(), vector.end(), element);
}

void draw_subdiv_and_voronoi(cv::Mat &img, cv::Subdiv2D& subdiv, cv::Scalar delaunay_color, std::vector<std::vector<cv::Point2f> > facets,
        std::vector<cv::Point2f> centers, cv::Scalar voronoi_color, std::vector<cv::Point> contour, std::vector<std::vector<cv::Point> > hole_contours)
{
	bool draw;
	std::vector < cv::Vec6f > triangleList;
	subdiv.getTriangleList(triangleList);
	std::vector < cv::Point > pt(3);

	//delaunay_diagram
	for (size_t i = 0; i < triangleList.size(); ++i)
	{
		cv::Vec6f t = triangleList[i];

		pt[0] = cv::Point(cvRound(t[0]), cvRound(t[1]));
		pt[1] = cv::Point(cvRound(t[2]), cvRound(t[3]));
		pt[2] = cv::Point(cvRound(t[4]), cvRound(t[5]));
		//make sure that Points out of image bounds are not drawn
		draw = true;

		for (int i = 0; i < 3; i++)
		{
			if (pt[i].x > img.cols || pt[i].y > img.rows || pt[i].x < 0 || pt[i].y < 0)
				draw = false;
		}
		if (draw)
		{
//			line(img, pt[0], pt[1], delaunay_color, 1);
//			line(img, pt[1], pt[2], delaunay_color, 1);
//			line(img, pt[2], pt[0], delaunay_color, 1);
		}
	}
	//draw voronoi_diagram
	for (int idx = 0; idx < facets.size(); idx++)
	{
		cv::Point2f last = facets[idx].back();
		//draw each line of the voronoi-cell
		for (int c = 0; c < facets[idx].size(); c++)
		{
			bool inside = true;
			cv::Point2f p = facets[idx][c];
			//only draw Points that are inside the contour
			if (cv::pointPolygonTest(contour, p, false) < 0 || cv::pointPolygonTest(contour, last, false) < 0)
			{
				inside = false;
			}
			//only draw Points inside the contour that are not inside a hole-contour
			for (int i = 0; i < hole_contours.size(); i++)
			{
				if (cv::pointPolygonTest(hole_contours[i], p, false) >= 0 || cv::pointPolygonTest(hole_contours[i], last, false) >= 0)
				{
					inside = false;
				}
			}
			if (inside)
			{
				line(img, last, p, voronoi_color, 1);
//				cv::imshow("test", img);
//				cv::waitKey(50);
			}
			last = p;
		}
	}
//	for (int idx = 0; idx < centers.size(); idx++)
//	{
//		cv::circle(img, centers[idx], 2, cv::Scalar(200), CV_FILLED);
//	}
}

cv::Mat create_delaunay_and_voronoi_graph(cv::Mat original_map)
{
	cv::Mat img = original_map.clone();
//	cv::Mat img = cv::Mat::zeros( original_map.rows, original_map.cols, CV_8UC1);
	//delaunay-variables
	cv::Rect rect(0, 0, img.cols, img.rows);
	cv::Subdiv2D subdiv(rect);
	cv::Scalar delaunay_color(50);
	//voronoi-variables
	std::vector < std::vector<cv::Point2f> > facets;
	std::vector < cv::Point2f > centers;
	cv::Scalar voronoi_color(127);
	//variables for contour extraction and discretisation
	std::vector < std::vector<cv::Point> > contours;
	std::vector < std::vector<cv::Point> > eroded_contours;
	std::vector < std::vector<cv::Point> > hole_contours;
	std::vector < cv::Point > largest_contour;
	std::vector < cv::Vec4i > hierarchy;
	cv::Mat eroded_map;
	cv::Point anchor(-1, -1);
	//map-clones
	cv::Mat temporary_map = original_map.clone();

	//get contour of the map
	cv::findContours(img, contours, hierarchy, CV_RETR_CCOMP, CV_CHAIN_APPROX_NONE);
	cv::drawContours(img, contours, -1, cv::Scalar(254), CV_FILLED);

	//draw found contours in the delaunay-subdivision graph
	for (int i = 0; i < contours.size(); i++)
	{
		for (int p = 0; p < contours[i].size(); p++)
		{
			cv::Point fp = contours[i][p];
			subdiv.insert(fp);
		}
		//get the contours of the black holes
		if (hierarchy[i][2] == -1 && hierarchy[i][3] != -1)
		{
			hole_contours.push_back(contours[i]);
		}
	}

	//erode the map and get the contour of it so that Points near the boundary are not drawn later
	//(see draw_subdiv_and_voronoi, section draw_voronoi)
	cv::erode(temporary_map, eroded_map, cv::Mat(), anchor, 4);
	cv::findContours(eroded_map, eroded_contours, hierarchy, CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE);
	largest_contour = contours[0];
	for (int i = 0; i < eroded_contours.size(); i++)
	{
		if (contourArea(largest_contour) < contourArea(eroded_contours[i]))
		{
			largest_contour = eroded_contours[i];
		}
		if (hierarchy[i][2] == -1 && hierarchy[i][3] != -1)
		{
			hole_contours.push_back(eroded_contours[i]);
		}
	}

	subdiv.getVoronoiFacetList(std::vector<int>(), facets, centers);
	draw_subdiv_and_voronoi(img, subdiv, delaunay_color, facets, centers, voronoi_color, largest_contour, hole_contours);

	for (int x = 0; x < img.rows; x++)
	{
		for (int y = 0; y < img.cols; y++)
		{
			if (original_map.at<unsigned char>(x, y) == 0)
			{
				img.at<unsigned char>(x, y) = 0;
			}
		}
	}

	return img;
}

cv::Mat segmentation_algorithm(cv::Mat voronoi_map, cv::Mat original_Map)
{
	cv::Mat map = voronoi_map.clone();
	double map_resolution_factor_from_subscription_ = 0.050000;
	double room_area_factor_lower_limit_ = 1.0; // eigentlich =3.0, aber dann wird oberster RechterRaum nicht als Raum erkannt, da area==2.0
	double room_area_factor_upper_limit_ = 40.0;
//	cv::Mat temporary_map = voronoi_map.clone();
	//variables for Point-extraction
	std::vector < cv::Point > node_Points;
	int neighbors;
	bool real_voronoi_point;
	//variables for finding critical points
	cv::Mat distance_map;
	cv::Point current_critical_Point;
	std::vector < cv::Point > critical_Points;
	std::vector<cv::Point> neighbor_Points, temporary_Points;
	int loopcounter, eps;
	//variables for critical lines
	cv::Mat temporary_map = original_Map.clone();
	std::vector < std::vector<cv::Point> > contours;
	cv::Point basis_Point_1, basis_Point_2;
	std::vector<cv::Point> basis_Points_1, basis_Points_2;
	double current_distance, distance_basis_1, distance_basis_2;
	double current_angle;
	std::vector<double> angles;
	int basis_vector_1_x, basis_vector_2_x, basis_vector_1_y, basis_vector_2_y;
	std::vector < cv::Point > already_drawn_Points;
	bool draw;
	//variables for coloring the map
	std::vector < cv::Vec4i > hierarchy;
	//
	//***************************extract the possible candidates for critical Points****************************
	//extract the node-Points that have at least three neighbors on the voronoi diagram
	for (int x = 0; x < map.rows; x++)
	{
		for (int y = 0; y < map.cols; y++)
		{
			if (map.at<unsigned char>(x, y) == 127)
			{
				neighbors = 0;
				for (int row_counter = -1; row_counter <= 1; row_counter++)
				{
					for (int column_counter = -1; column_counter <= 1; column_counter++)
					{
						if (map.at<unsigned char>(x + row_counter, y + column_counter) == 127 && (abs(row_counter) + abs(column_counter)) != 0)
						{
							neighbors++;
						}
					}
				}
				if (neighbors > 2)
				{
					node_Points.push_back(cv::Point(x, y));
				}
			}
		}
	}
	//reduce the side-lines along the voronoi-graph by checking if it has only one neighbor --> make it white
	//repeat a large enough number of times
	for (int a = 0; a < 100; a++)
	{
		for (int x = 0; x < map.rows; x++)
		{
			for (int y = 0; y < map.cols; y++)
			{
				real_voronoi_point = true;
				if (map.at<unsigned char>(x, y) == 127)
				{
					neighbors = 0;
					for (int row_counter = -1; row_counter <= 1; row_counter++)
					{
						for (int column_counter = -1; column_counter <= 1; column_counter++)
						{
							if (map.at<unsigned char>(x + row_counter, y + column_counter) == 127 && (abs(row_counter) + abs(column_counter)) != 0)
							{
								neighbors++;
							}
						}
					}
					if (neighbors == 1)
					{
						real_voronoi_point = false;
					}
					//if the current Point is a previous found node Point it belongs to the voronoi-graph
					if (contains(node_Points, cv::Point(x, y)))
					{
						real_voronoi_point = true;
					}

					if (!real_voronoi_point)
					{
						map.at<unsigned char>(x, y) = 254;
					}
				}
			}
		}
	}

	// find the critical Points in the previously calculated possible critical Points by searching in a specified neighborhood
	// for the local Minimum of distance to nearest black Pixel
	//critical Points need to have at least two neighbors (else they are endpoints, which would give a very small segment)

	//get the distance transformed map, which shows the distance of every white Pixel to the closest zero-Pixel
	cv::distanceTransform(map, distance_map, CV_DIST_L2, 5);
	cv::convertScaleAbs(distance_map, distance_map);

	for (int x = 0; x < map.rows; x++)
	{
		for (int y = 0; y < map.cols; y++)
		{
			if (map.at<unsigned char>(x, y) == 127)
			{
				eps = 310 / (int) distance_map.at<unsigned char>(x, y); //310
				loopcounter = 0;
				neighbor_Points.clear();
				neighbors = 0;
				neighbor_Points.push_back(cv::Point(x, y));
				//find every Point along the voronoi graph in a specified neighborhood
				do
				{
					loopcounter++;
					for (int p = 0; p < neighbor_Points.size(); p++)
					{
						for (int row_counter = -1; row_counter <= 1; row_counter++)
						{
							for (int column_counter = -1; column_counter <= 1; column_counter++)
							{
								if (map.at<unsigned char>(neighbor_Points[p].x + row_counter, neighbor_Points[p].y + column_counter) == 127
								        && (abs(row_counter) + abs(column_counter)) != 0
								        && !contains(neighbor_Points, cv::Point(neighbor_Points[p].x + row_counter, neighbor_Points[p].y + column_counter)))
								{
									neighbors++;
									temporary_Points.push_back(cv::Point(neighbor_Points[p].x + row_counter, neighbor_Points[p].y + column_counter));
								}
							}
						}
					}
					for (int tp = 0; tp < temporary_Points.size(); tp++)
					{
						neighbor_Points.push_back(temporary_Points[tp]);
						map.at<unsigned char>(cv::Point(temporary_Points[tp].y, temporary_Points[tp].x)) = 254;
						map.at<unsigned char>(cv::Point(y, x)) = 254;
					}
					temporary_Points.clear();
				} while (neighbors <= eps && loopcounter < 150);
				//check every found Point in the neighborhood if it is the local Minimum in the distanceMap
				current_critical_Point = cv::Point(x, y);
				for (int p = 0; p < neighbor_Points.size(); p++)
				{
					if (distance_map.at<unsigned char>(neighbor_Points[p].x, neighbor_Points[p].y)
					        < distance_map.at<unsigned char>(current_critical_Point.x, current_critical_Point.y))
					{
						current_critical_Point = cv::Point(neighbor_Points[p]);
					}
				}
				critical_Points.push_back(current_critical_Point);
			}
		}
	}
//	for (int i = 0; i < critical_Points.size(); i++)
//	{
//		cv::circle(new_map, cv::Point(critical_Points[i].y, critical_Points[i].x), 1, cv::Scalar(0), CV_FILLED);
//	}
	//
	//*************draw the critical lines from every found critical Point to its two closest zero-Pixel****************
	//
	// 1. Get the Points of the contour, which are the possible closest Points for a critical Point
	cv::findContours(temporary_map, contours, CV_RETR_CCOMP, CV_CHAIN_APPROX_NONE);
	cv::drawContours(temporary_map, contours, -1, cv::Scalar(254), CV_FILLED);

	//2. Get the basis-points for each critical-point
	for (int cp = 0; cp < critical_Points.size(); cp++)
	{
		//set inital Points for the basis-Points so the distance comparisation can be done
		draw = true;
		current_angle = 0;
		basis_Point_1 = contours[0][0];
		basis_Point_2 = contours[0][1];
		double vector_x_1 = critical_Points[cp].x - contours[0][0].y;
		double vector_y_1 = critical_Points[cp].y - contours[0][0].x;
		distance_basis_1 = std::sqrt((std::pow(vector_x_1, 2)) + std::pow(vector_y_1, 2));
		double vector_x_2 = critical_Points[cp].x - contours[0][1].y;
		double vector_y_2 = critical_Points[cp].y - contours[0][1].x;
		distance_basis_2 = std::sqrt((std::pow(vector_x_2, 2)) + std::pow(vector_y_2, 2));

		//find first basis-point
		for (int c = 0; c < contours.size(); c++)
		{
			for (int p = 0; p < contours[c].size(); p++)
			{
				//calculate the euclidian distance from the critical Point to the Point on the contour
				double vector_x = critical_Points[cp].x - contours[c][p].y;
				double vector_y = critical_Points[cp].y - contours[c][p].x;
				current_distance = std::sqrt((std::pow(vector_x, 2)) + std::pow(vector_y, 2));
				//compare the distance to the saved distances if it is smaller
				if (current_distance < distance_basis_1)
				{
					distance_basis_1 = current_distance;
					basis_Point_1 = contours[c][p];
					basis_vector_1_x = -1 * vector_x;
					basis_vector_1_y = -1 * vector_y;
				}
			}
		}
		//find second basis-point
		for (int c = 0; c < contours.size(); c++)
		{
			for (int p = 0; p < contours[c].size(); p++)
			{
				//calculate the euclidian distance from the critical Point to the Point on the contour
				double vector_x = critical_Points[cp].x - contours[c][p].y;
				double vector_y = critical_Points[cp].y - contours[c][p].x;
				current_distance = std::sqrt((std::pow(vector_x, 2)) + std::pow(vector_y, 2));
				//calculate the distance between the current contour Point and the first Basis Point to make sure they
				//are not too close to each other
				double vector_x_basis = basis_Point_1.y - contours[c][p].y;
				double vector_y_basis = basis_Point_1.x - contours[c][p].x;
				double basis_distance = std::sqrt((std::pow(vector_x_basis, 2)) + std::pow(vector_y_basis, 2));
				if (current_distance > distance_basis_1 && current_distance < distance_basis_2
				        && basis_distance > (double) distance_map.at<unsigned char>(cv::Point(critical_Points[cp].y, critical_Points[cp].x)))
				{
					distance_basis_2 = current_distance;
					basis_Point_2 = contours[c][p];
					basis_vector_2_x = -1 * vector_x;
					basis_vector_2_y = -1 * vector_y;
				}
			}
		}
		//calculate angle between the vectors from the critical Point to the found basis-points
		double g = basis_vector_1_x * basis_vector_2_x;
		double h = basis_vector_1_y * basis_vector_2_y;
		current_angle = std::acos((g + h) / (distance_basis_1 * distance_basis_2)) * 180.0 / PI;

		basis_Points_1.push_back(basis_Point_1);
		basis_Points_2.push_back(basis_Point_2);
		angles.push_back(current_angle);

	}

	//3. check which critical Points should be used for the segmentation. This is done by cheking the Points that are
	//in a specified distance to each other and take the Point with the largest calculated angle, because larger angles
	//corresponend to a seperation across the room, which is more useful
	for (int cp = 0; cp < critical_Points.size(); cp++)
	{
		//reset variable for checking if the line should be drawn
		draw = true;
		for (int i = 0; i < critical_Points.size(); i++)
		{
			if (i != cp)
			{
				//get distance of the two current Points
				double vector_x = critical_Points[i].x - critical_Points[cp].x;
				double vector_y = critical_Points[i].y - critical_Points[cp].y;
				double critical_Point_distance = std::sqrt((std::pow(vector_x, 2)) + std::pow(vector_y, 2));
				//check if the Points are close enough
				if (critical_Point_distance < 26.0) //26.0
				{
					//if one Point in neighborhood is found that has a larger angle the actual to-be-checked Point shouldn't be drawn
					if (angles[cp] < angles[i])
					{
						draw = false;
					}
				}
			}
		}
		//draw critical-lines if angle of Point is larger than the other
		if (draw)
		{
			cv::line(map, cv::Point(critical_Points[cp].y, critical_Points[cp].x), basis_Points_1[cp], cv::Scalar(0));
			cv::line(map, cv::Point(critical_Points[cp].y, critical_Points[cp].x), basis_Points_2[cp], cv::Scalar(0));
		}
	}
	cv::Mat testmap = map.clone();
	for (int i = 0; i < critical_Points.size(); i++)
	{
		cv::circle(testmap, cv::Point(critical_Points[i].y, critical_Points[i].x), 1, cv::Scalar(0), CV_FILLED);
	}
	cv::imwrite("/home/rmb-fj/Pictures/maps/Delaunay_medial_axis/critical_lines.jpg", testmap);

	//4. Draw the segmentation into the map by finding the contours of the segments and drawing it with a random color
	//into the map
	cv::erode(map, map, cv::Mat(), cv::Point(-1, -1), 1);
	cv::findContours(map, contours, hierarchy, CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE);

	for (int c = 0; c < contours.size(); c++)
	{ //only draw contours that don't belong to holes
		if (hierarchy[c][3] == -1)
		{
			//calculate area for the contour and check if it is large enough to be a room
			double room_area = map_resolution_factor_from_subscription_ * map_resolution_factor_from_subscription_ * cv::contourArea(contours[c]);
			if (room_area >= room_area_factor_lower_limit_ && room_area <= room_area_factor_upper_limit_)
			{
				cv::Scalar fill_colour(rand() % 200 + 53);
//				cv::Scalar fill_colour(150);
				cv::drawContours(temporary_map, contours, c, fill_colour, CV_FILLED);
			}
		}
	}

	//5.fill the last white areas with the surrounding color
	cv::Mat temporary_map_to_fill_white_pixels_ = temporary_map.clone();
	for (int loop_counter = 0; loop_counter < 100; loop_counter++)
	{
		for (int column = 0; column < temporary_map.cols; column++)
		{
			for (int row = 0; row < temporary_map.rows; row++)
			{
				if (temporary_map.at<unsigned char>(row, column) == 254)
				{
					//check 3x3 area around white pixel for fillcolor, if filled Pixel around fill white pixel with that color
					for (int row_counter = -1; row_counter <= 1; row_counter++)
					{
						for (int column_counter = -1; column_counter <= 1; column_counter++)
						{
							if (temporary_map.at<unsigned char>(row + row_counter, column + column_counter) != 0
							        && temporary_map.at<unsigned char>(row + row_counter, column + column_counter) != 254)
							{
								temporary_map_to_fill_white_pixels_.at<unsigned char>(row, column) = temporary_map.at<unsigned char>(row + row_counter,
								        column + column_counter);
							}
						}
					}
				}
			}
		}
		temporary_map = temporary_map_to_fill_white_pixels_.clone();
	}
	map = temporary_map_to_fill_white_pixels_.clone();
	return map;
}

int main(int argc, char **argv)
{
	ros::init(argc, argv, "delaunay_segmentation");
	ros::NodeHandle n;
//ros::ServiceServer service = n.advertiseService("roomsegmentation", segmentation_algorithm);
	ROS_INFO("Delaunay-Tringaulation einer Diskretisierung eines Polygons");
//	Delaunay_medial_axis/testrectangle.jpg
	cv::Mat map = cv::imread("/home/rmb-fj/Pictures/maps/black_map.png", 0);
	for (int y_coordinate = 0; y_coordinate < map.cols; y_coordinate++)
	{
		for (int x_coordinate = 0; x_coordinate < map.rows; x_coordinate++)
		{
			//find not reachable regions andmake them black
			if (map.at<unsigned char>(x_coordinate, y_coordinate) != 254)
			{
				map.at<unsigned char>(x_coordinate, y_coordinate) = 0;
			}
		}
	}
	cv::Mat segmented_map;
	cv::erode(map, map, cv::Mat());
	cv::dilate(map, map, cv::Mat());
	cv::Mat delaunay;
	if (map.empty())
	{
		ROS_INFO("Fehler Bildladen");
		return -1;
	}

	delaunay = create_delaunay_and_voronoi_graph(map);
	std::cout << "done makin delauny and voronoi" << std::endl;

//	cv::imshow("test", delaunay);
	cv::imwrite("/home/rmb-fj/Pictures/maps/Delaunay_medial_axis/delaunay_and_voronoi_graph.jpg", delaunay);
//	cv::waitKey(100000);
	//ros::spin();
	segmented_map = segmentation_algorithm(delaunay, map);
	std::cout << "done segmenting the map" << std::endl;
	cv::imwrite("/home/rmb-fj/Pictures/maps/Delaunay_medial_axis/segmented_map.jpg", segmented_map);

	return 0;
}
