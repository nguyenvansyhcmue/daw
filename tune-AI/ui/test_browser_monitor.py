from core.app_controller import AppController


def main():
    controller = AppController()

    video = controller.get_current_external_youtube_video()

    if not video:
        print("Chưa đọc được video YouTube từ Brave.")
        print("Hãy mở một video YouTube trong Brave do app tự mở.")
        return

    print("Đã đọc được video:")
    print("Video ID:", video.get("video_id"))
    print("URL:", video.get("url"))
    print("Title:", video.get("title"))


if __name__ == "__main__":
    main()