#!/bin/python3
"""Simple test script for OpenCV solvePnP using the SQPNP algorithm.

Edit the `OBJECT_POINTS` and `IMAGE_POINTS` lists below to try different cases.
Run: `python3 test_pnp.py` or make executable and run `./test_pnp.py`.
"""

import numpy as np
import cv2
import argparse


def parse_args():
    p = argparse.ArgumentParser(description="Test solvePnP with SQPNP")
    p.add_argument("--show", action="store_true", help="Print projected points and residuals")
    p.add_argument("--camera-yaml", type=str, default=None, help="Path to ROS camera calibration YAML file")
    return p.parse_args()


HALF_EDGE = 0.1650 / 2.0

# tag center in world frame. Tag is yawed 180 around z
TAG_ORIGIN_TF = np.array(
    [
        [-1.0, 0.0, 0.0, 4.022],
        [0.0, -1.0, 0.0, 4.035],
        [0.0, 0.0, 1.0, 1.124],
        [0.0, 0.0, 0.0, 1.0],
    ]
)

# CAMERA TO WORLD Rotation, yaw 90, pitch -90 roll 0,
CAM_TO_WORLD_R_TF = np.array(
    [
        [0.0, 0.0, 1.0, 0.0],
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]
)


# TAG_ORIGIN_TF = np.array(
#     [
#         [1.0, 0.0, 0.0, 0.0],
#         [0.0, 1.0, 0.0, 0.0],
#         [0.0, 0.0, 1.0, 0.0],
#         [0.0, 0.0, 0.0, 1.0],
#     ]
# )

CAMERA_FRAME_PTS = [
    # order is BL, BR, TR, TL
    (0.0, -HALF_EDGE, HALF_EDGE),
    (0.0, -HALF_EDGE, -HALF_EDGE),
    (0.0, HALF_EDGE, -HALF_EDGE),
    (0.0, HALF_EDGE, HALF_EDGE),
]

# transform the tag points into the world frame by applying a known rotation and translation
OBJECT_POINTS_WITH_TF = []
for pt in CAMERA_FRAME_PTS:
    homogeneous_pt = np.array([pt[0], pt[1], pt[2], 1.0])
    world_pt = TAG_ORIGIN_TF @ homogeneous_pt
    OBJECT_POINTS_WITH_TF.append(tuple(world_pt[:3]))

# Corresponding 2D image points in pixel coordinates. Keep the same order as 3D points.
IMAGE_POINTS = [
    (962.61, 652.18),  # BL
    (1008.96, 657.21),  # BR
    (1013.66, 611.01),  # TR
    (967.22, 605.97),  # TL
    # (1063.85, 664.08),
    # (1110.09, 669.90),
    # (1115.44, 623.84),
    # (1069.41, 618.03),
]


def to_numpy_points(obj_pts, img_pts):
    obj = np.asarray(obj_pts, dtype=np.float64)
    img = np.asarray(img_pts, dtype=np.float64)
    if obj.ndim != 2 or obj.shape[1] != 3:
        raise ValueError("object points must be a list of 3D points")
    if img.ndim != 2 or img.shape[1] != 2:
        raise ValueError("image points must be a list of 2D points")
    return obj, img


def solve_sq_pnp(object_points, image_points, camera_matrix, dist_coefs):
    flag = getattr(cv2, "SOLVEPNP_SQPNP", None)
    if flag is None:
        raise RuntimeError("OpenCV build does not provide SOLVEPNP_SQPNP flag")

    # OpenCV accepts (N,3) and (N,2)
    success, rvec, tvec = cv2.solvePnP(
        object_points,
        image_points,
        camera_matrix,
        dist_coefs,
        flags=flag,
    )
    return bool(success), rvec, tvec


def load_camera_from_yaml(path):
    try:
        import yaml
    except Exception as e:
        raise RuntimeError("PyYAML is required to load camera YAML. Install with `pip install pyyaml`") from e

    with open(path, "r") as f:
        data = yaml.safe_load(f)

    # Typical ROS camera_info yaml has keys 'camera_matrix' and 'distortion_coefficients'
    if "camera_matrix" in data and "data" in data["camera_matrix"]:
        cam_data = data["camera_matrix"]["data"]
    elif "camera_matrix" in data and "K" in data["camera_matrix"]:
        cam_data = data["camera_matrix"]["K"]
    elif "K" in data:
        cam_data = data["K"]
    else:
        raise ValueError("camera_matrix not found in YAML")

    cam = np.array(cam_data, dtype=np.float64).reshape((3, 3))

    # Distortion coefficients
    if "distortion_coefficients" in data and "data" in data["distortion_coefficients"]:
        dist = data["distortion_coefficients"]["data"]
    elif "D" in data:
        dist = data["D"]
    elif "distortion_coeffs" in data:
        dist = data["distortion_coeffs"]
    else:
        # default to zeros if not present
        raise ValueError("distortion_coefficients not found in YAML")

    dist = np.asarray(dist, dtype=np.float64).reshape(-1, 1)
    return cam, dist


def reprojection_error(object_points, image_points, rvec, tvec, camera_matrix, dist_coefs):
    projected, _ = cv2.projectPoints(object_points, rvec, tvec, camera_matrix, dist_coefs)
    projected = projected.reshape(-1, 2)
    diffs = image_points - projected
    errors = np.linalg.norm(diffs, axis=1)
    return float(np.mean(errors)), errors, projected


def rvec_tvec_to_matrix(rvec, tvec):
    """Convert rotation vector and translation vector to 4x4 homogeneous matrix."""
    R, _ = cv2.Rodrigues(rvec)
    T = np.eye(4)
    T[:3, :3] = R
    T[:3, 3] = tvec.flatten()
    return T


def matrix_to_rvec_tvec(T):
    """Convert 4x4 homogeneous matrix to rotation vector and translation vector."""
    R = T[:3, :3]
    tvec = T[:3, 3].reshape(3, 1)
    rvec, _ = cv2.Rodrigues(R)
    return rvec, tvec


def main():
    args = parse_args()

    with_tf = to_numpy_points(OBJECT_POINTS_WITH_TF, IMAGE_POINTS)
    without_tf = to_numpy_points(CAMERA_FRAME_PTS, IMAGE_POINTS)

    if not args.camera_yaml:
        print("Need --camera-yaml to load camera intrinsics")
        return

    cam, dist = load_camera_from_yaml(args.camera_yaml)

    try:
        ok1, rvec_with_tf, tvec_with_tf = solve_sq_pnp(*with_tf, cam, dist)
        ok2, rvec_no_tf, tvec_no_tf = solve_sq_pnp(*without_tf, cam, dist)
    except Exception as e:
        print("Error running SQPNP solvePnP:", e)
        return

    if not (ok1 and ok2):
        print("solvePnP failed to converge")
        return

    # Convert the with_tf solution to a transformation matrix (world->camera)
    T_cam_world_direct = rvec_tvec_to_matrix(rvec_with_tf, tvec_with_tf)
    
    # Convert the without_tf solution to a transformation matrix (tag->camera)
    T_cam_tag = CAM_TO_WORLD_R_TF @ rvec_tvec_to_matrix(rvec_no_tf, tvec_no_tf)
    
    # Transform the without_tf solution to world frame
    # T_cam_world = T_cam_tag * T_tag_world = T_cam_tag * inv(T_world_tag)
    T_world_tag = TAG_ORIGIN_TF
    T_tag_world = np.linalg.inv(T_world_tag)
    T_cam_world_transformed = T_cam_tag @ T_tag_world

    print("=" * 60)
    # print tag to world and cam to tag for debugging
    print("T_world_tag (tag pose in world frame):")
    print(TAG_ORIGIN_TF)
    
    # Show what happens to point (0,0,0) when transformed by TAG_ORIGIN_TF
    point_origin = np.array([0.0, 0.0, 0.0, 1.0])
    transformed_origin = TAG_ORIGIN_TF @ point_origin
    print(f"\nPoint (0,0,0) transformed by TAG_ORIGIN_TF: {transformed_origin[:3]}")
    print(f"  -> World coordinates: x={transformed_origin[0]:.3f}, y={transformed_origin[1]:.3f}, z={transformed_origin[2]:.3f}")
    
    # Compare the two solutions
    print("Tag pose in camera frame (without_tf, un-transformed):")
    print(T_cam_tag)
    print("\n" + "=" * 60)
    print("Camera pose in world frame (from with_tf):")
    print(np.linalg.inv(T_cam_world_direct))
    print("\nCamera pose in world frame (from without_tf, transformed):")
    print(T_cam_world_transformed)
    print("\nDifference:")
    diff = T_cam_world_direct - T_cam_world_transformed
    print(diff)
    print(f"\nFrobenius norm of difference: {np.linalg.norm(diff):.6f}")
    
    # Calculate reprojection errors
    mean_err_with, per_point_err_with, projected_with = reprojection_error(
        with_tf[0], with_tf[1], rvec_with_tf, tvec_with_tf, cam, dist
    )
    mean_err_no, per_point_err_no, projected_no = reprojection_error(
        without_tf[0], without_tf[1], rvec_no_tf, tvec_no_tf, cam, dist
    )
    
    print(f"\nReprojection error (with_tf): {mean_err_with:.4f} pixels")
    print(f"Reprojection error (without_tf): {mean_err_no:.4f} pixels")

    if args.show:
        print("\n" + "=" * 60)
        print("Per-point reprojection errors (with_tf):")
        for i, (gt, proj, e) in enumerate(zip(with_tf[1], projected_with, per_point_err_with)):
            print(f"  Point {i}: GT={tuple(gt)} -> PROJ={tuple(proj.round(2))}, err={e:.4f}")
        
        print("\nPer-point reprojection errors (without_tf):")
        for i, (gt, proj, e) in enumerate(zip(without_tf[1], projected_no, per_point_err_no)):
            print(f"  Point {i}: GT={tuple(gt)} -> PROJ={tuple(proj.round(2))}, err={e:.4f}")


if __name__ == "__main__":
    main()
