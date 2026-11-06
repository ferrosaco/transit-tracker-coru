FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY itranvias_adapter ./itranvias_adapter

ENV PORT=8080
EXPOSE 8080
CMD ["python", "-m", "itranvias_adapter"]
