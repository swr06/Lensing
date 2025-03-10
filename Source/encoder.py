import cv2
import os

def images_to_video(folder='frames', output='output.mp4', fps=60):
    images = sorted([img for img in os.listdir(folder) if img.endswith('.jpg')], key=lambda x: int(x.split('.')[0]))
    
    if not images:
        print("No images found in the folder.")
        return
    
    first_frame = cv2.imread(os.path.join(folder, images[0]))
    height, width, _ = first_frame.shape
    
    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    video = cv2.VideoWriter(output, fourcc, fps, (width, height))
    
    total_images = len(images)
    for i, image in enumerate(images):
        frame = cv2.imread(os.path.join(folder, image))
        video.write(frame)
        progress = (i + 1) / total_images * 100
        print(f"Processing: {progress:.2f}%", end='\r')
    
    video.release()
    print(f"\nVideo saved as {output}")

if __name__ == "__main__":
    images_to_video()
