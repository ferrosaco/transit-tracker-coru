FROM python:3.12-slim

WORKDIR /app
COPY api/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY api/itranvias_adapter ./itranvias_adapter
COPY visor/index.html ./itranvias_adapter/preview.html

ENV PORT=8080
EXPOSE 8080
CMD ["python", "-m", "itranvias_adapter"]
