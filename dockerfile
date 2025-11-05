# 1. ใช้ Ubuntu ล่าสุดเป็น base image
FROM ubuntu:latest

# 2. ไม่ให้ interactive prompt (สำคัญเวลา apt install)
ENV DEBIAN_FRONTEND=noninteractive

# 3. อัปเดต package และติดตั้ง gcc, g++, bc
RUN apt-get update && \
    apt-get install -y gcc g++ bc && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# 4. ตั้ง working directory ใน container
WORKDIR /app

# 5. คัดลอกไฟล์ทั้งหมดจากโฟลเดอร์ปัจจุบันไป container
COPY . .

# 6. ค่าเริ่มต้นเมื่อรัน container
CMD ["bash"]
